/****************************************************************************
 * app/velatime/tools/time_sync_main.c
 *
 * 开机自动校时 —— 让表盘时间和现实一致。
 *
 * 背景：
 *   BES2800BP 没有带电池的 RTC，上电后系统时钟是 1970-01-01。
 *   VelaTime 表盘直接读系统时钟，所以开机后显示的时间是错的。
 *   NSH 的 `date` 命令只读（`date -s` 不支持），无法手动设。
 *
 * 做法：
 *   用【明文 HTTP】（不是 HTTPS）向一个网站发 HEAD 请求，
 *   从响应头里读 `Date:` 字段。明文 HTTP 不需要 TLS，
 *   因此【不依赖系统时钟是否正确】—— 这正是关键：
 *   HTTPS 校时会被证书校验挡住，而 HTTP 不会。
 *
 * 用法：
 *   time_sync            # 依次尝试内置的几个主机
 *   time_sync <host>     # 指定主机
 *
 * 退出码：0 成功，非 0 失败（rcS 忽略失败）
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TAG "time_sync"

/* 默认尝试的主机（明文 HTTP，80 端口） */
static const char *g_hosts[] =
{
  "www.baidu.com",
  "www.taobao.com",
  "www.qq.com",
  NULL
};

static const char *g_months[] =
{
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* 从 socket 读一行（到 \n），返回长度；0 表示对端关闭 */
static int read_line(int fd, char *buf, int cap)
{
  int n = 0;

  while (n < cap - 1)
    {
      char    c;
      ssize_t r = recv(fd, &c, 1, 0);

      if (r <= 0)
        {
          break;
        }

      buf[n++] = c;

      if (c == '\n')
        {
          break;
        }
    }

  buf[n] = '\0';
  return n;
}

/* 解析 "Sat, 20 Sep 2026 12:34:56 GMT" -> UTC epoch；失败返回 0 */
static time_t parse_http_date(const char *s)
{
  int       day;
  int       year;
  int       hour;
  int       min;
  int       sec;
  char      mon_str[8];
  int       mon = -1;
  int       i;
  struct tm tm_utc;
  time_t    t;

  /*
   * 注意：不要用 %*[^,] 这种字符类转换 —— NuttX 的 libc scanf
   * 对它支持不好，整条会直接失败（实测报 "bad Date header"）。
   * 这里先手工跳过 "Sat, " 前缀（也就是第一个空格之前的部分）。
   * 输入形如： "Sat, 19 Sep 2026 15:05:04 GMT"
   */
  {
    const char *p = strchr(s, ' ');

    if (p == NULL)
      {
        return (time_t)0;
      }

    p++;    /* 跳过 "Sat," 后面那个空格 */

    if (sscanf(p, "%d %7s %d %d:%d:%d",
               &day, mon_str, &year, &hour, &min, &sec) != 6)
      {
        return (time_t)0;
      }
  }

  for (i = 0; i < 12; i++)
    {
      if (strcmp(mon_str, g_months[i]) == 0)
        {
          mon = i;
          break;
        }
    }

  if (mon < 0)
    {
      return (time_t)0;
    }

  memset(&tm_utc, 0, sizeof(tm_utc));
  tm_utc.tm_sec  = sec;
  tm_utc.tm_min  = min;
  tm_utc.tm_hour = hour;
  tm_utc.tm_mday = day;
  tm_utc.tm_mon  = mon;
  tm_utc.tm_year = year - 1900;

  t = timegm(&tm_utc);
  return (t > 0) ? t : (time_t)0;
}

/* 向 host:80 发 HEAD，取 Date 头。成功返回 epoch，失败返回 0 */
static time_t fetch_from(const char *host)
{
  struct addrinfo  hints;
  struct addrinfo *res = NULL;
  struct addrinfo *ai;
  int              fd = -1;
  time_t           result = (time_t)0;
  char             req[256];
  char             line[512];
  int              n;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host, "80", &hints, &res) != 0 || res == NULL)
    {
      printf("%s: DNS failed for %s\n", TAG, host);
      return (time_t)0;
    }

  for (ai = res; ai != NULL; ai = ai->ai_next)
    {
      fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

      if (fd < 0)
        {
          continue;
        }

      if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
        {
          break;
        }

      close(fd);
      fd = -1;
    }

  freeaddrinfo(res);

  if (fd < 0)
    {
      printf("%s: connect failed for %s\n", TAG, host);
      return (time_t)0;
    }

  snprintf(req, sizeof(req),
           "HEAD / HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: VelaTime\r\n"
           "Connection: close\r\n\r\n",
           host);

  if (send(fd, req, strlen(req), 0) < 0)
    {
      printf("%s: send failed\n", TAG);
      close(fd);
      return (time_t)0;
    }

  while ((n = read_line(fd, line, sizeof(line))) > 0)
    {
      if (n <= 2)
        {
          break;                /* 空行 = 响应头结束 */
        }

      if (strncasecmp(line, "Date:", 5) == 0)
        {
          const char *v = line + 5;

          while (*v == ' ')
            {
              v++;
            }

          result = parse_http_date(v);

          if (result > 0)
            {
              printf("%s: %s -> %ld\n", TAG, host, (long)result);
            }
          else
            {
              printf("%s: bad Date header: %s", TAG, v);
            }

          break;
        }
    }

  close(fd);
  return result;
}

int main(int argc, FAR char *argv[])
{
  time_t         t = (time_t)0;
  struct timeval tv;
  struct tm      lt;
  time_t         local_epoch;
  char           buf[64];
  int            i;

  if (argc > 1)
    {
      t = fetch_from(argv[1]);
    }
  else
    {
      for (i = 0; g_hosts[i] != NULL; i++)
        {
          t = fetch_from(g_hosts[i]);

          if (t > 0)
            {
              break;
            }
        }
    }

  if (t <= 0)
    {
      printf("%s: failed to get time from network\n", TAG);
      return 1;
    }

  tv.tv_sec  = t;
  tv.tv_usec = 0;

  if (settimeofday(&tv, NULL) != 0)
    {
      printf("%s: settimeofday failed: %d\n", TAG, errno);
      return 2;
    }

  /* 用 UTC+8 手工换算，避开 NuttX 上 zoneinfo 查找失败的问题 */
  local_epoch = t + 8 * 3600;
  gmtime_r(&local_epoch, &lt);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
  printf("%s: clock set to %s CST (UTC+8)\n", TAG, buf);

  return 0;
}
