#include "ui_mock.h"

velatime_task_t mock_tasks[VELATIME_MOCK_TASK_COUNT] =
{
  {
    .id                = "task_001",
    .title             = "高数作业",
    .course            = "高等数学",
    .deadline          = "2026-09-20 18:00",
    .estimated_minutes = 30,
    .priority          = "high",
    .status            = VELATIME_STATUS_WAITING
  },
  {
    .id                = "task_002",
    .title             = "实验报告",
    .course            = "大学物理",
    .deadline          = "2026-09-20 22:00",
    .estimated_minutes = 60,
    .priority          = "high",
    .status            = VELATIME_STATUS_DOING
  },
  {
    .id                = "task_003",
    .title             = "英语单词",
    .course            = "大学英语",
    .deadline          = "2026-09-21 20:00",
    .estimated_minutes = 15,
    .priority          = "low",
    .status            = VELATIME_STATUS_WAITING
  },
  {
    .id                = "task_004",
    .title             = "课程论文",
    .course            = "软件工程",
    .deadline          = "2026-09-22 12:00",
    .estimated_minutes = 120,
    .priority          = "medium",
    .status            = VELATIME_STATUS_POSTPONED
  },
  {
    .id                = "task_005",
    .title             = "预习讲义",
    .course            = "数据结构",
    .deadline          = "2026-09-20 23:00",
    .estimated_minutes = 20,
    .priority          = "medium",
    .status            = VELATIME_STATUS_DONE
  }
};

velatime_course_t mock_courses[VELATIME_MOCK_COURSE_COUNT] =
{
  { "高等数学", 1, "08:00", "09:40" },
  { "大学英语", 1, "10:00", "11:40" },
  { "大学物理", 2, "08:00", "09:40" },
  { "软件工程", 2, "14:00", "15:40" },
  { "数据结构", 3, "10:00", "11:40" },
  { "大学体育", 4, "16:00", "17:40" },
  { "思修",     5, "08:00", "09:40" },
  { "实验课",   6, "09:00", "11:00" }
};

velatime_recommend_t mock_recommend =
{
  .task_id           = "task_001",
  .task_title        = "高数作业",
  .available_minutes = 40,
  .suggested_start   = "15:20",
  .reason            = "今天18:00截止，空闲40分钟足够完成"
};
