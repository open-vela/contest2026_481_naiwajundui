/****************************************************************************
 * app/velatime/include/velatime_time.h
 *
 * 统一的时间换算工具（北京时间 / UTC+8）。
 *
 * 为什么需要这个文件
 * --------------------------------------------------------------------------
 * BES2800BP 的系统时钟是 UTC，而表盘、任务截止时间、推荐引擎都按北京
 * 时间显示和计算。直觉做法是用 localtime_r()，但它在 NuttX 上不可靠：
 *
 *   - localtime_r() 依赖 TZ 环境变量；
 *   - NuttX 遇到 POSIX 风格 TZ 串（如 "CST-8"）时会去找 zoneinfo 文件，
 *     找不到就静默退回 UTC；
 *   - 结果就是所有时间都比北京时间慢 8 小时。
 *
 * 所以这里不依赖 TZ，而是手工加减 8 小时：
 *   velatime_localtime() : UTC epoch  -> 北京时间的 struct tm
 *   velatime_mktime()    : 北京时间的 struct tm -> UTC epoch
 *
 * 这两个函数是互逆的，项目里所有时间换算都必须走它们。
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

#ifndef __APP_VELATIME_INCLUDE_VELATIME_TIME_H
#define __APP_VELATIME_INCLUDE_VELATIME_TIME_H

#include <time.h>

/* 北京时间相对 UTC 的偏移（秒） */
#define VELATIME_TZ_OFFSET_SEC  (8 * 3600)

/*
 * UTC epoch -> 北京时间的 struct tm。
 * 等价于"TZ 正确时的 localtime_r()"，但不依赖 TZ。
 * 成功返回 out，失败返回 NULL。
 */
static inline struct tm *velatime_localtime(time_t t, struct tm *out)
{
  time_t local_epoch = t + VELATIME_TZ_OFFSET_SEC;

  return gmtime_r(&local_epoch, out);
}

/*
 * 北京时间的 struct tm -> UTC epoch。
 * 等价于"TZ 正确时的 mktime()"，但不依赖 TZ。
 * 注意：不做 mktime 那种"就地规范化"，所以调用方若要跨月/跨年
 * 递推日期，请自己对 tm_mday 加减后再传进来（timegm 能正确处理
 * 越界的 tm_mday/tm_mon）。
 */
static inline time_t velatime_mktime(const struct tm *tm_beijing)
{
  struct tm tmp = *tm_beijing;

  tmp.tm_isdst = 0;

  return timegm(&tmp) - VELATIME_TZ_OFFSET_SEC;
}

#endif /* __APP_VELATIME_INCLUDE_VELATIME_TIME_H */
