if(CONFIG_EXAMPLES_VELATIME)
  # 字库严格按设计规范，全部由 lv_font_conv 从官方字体源生成：
  #   12 个小时数字  Roboto Light   300  56px  仅 0-9        （21.5KB）
  #   时间           Roboto Regular 400  72px  数字与冒号     （28.4KB）
  #   日期/辅助数字   Roboto Regular 400  16px  ASCII         （41.1KB）
  #   中文页面入口    Noto Sans SC   400  16px  任务/首页/课表  （7.3KB）
  #   中文正文        Noto Sans SC   400  16px  234 个实际用字  （205KB）
  # 合计约 297KB。原 simhei 中文字库为 3.2MB，已被替换。
  # 生成方法见 C:\xiaomi\字库生成方法.md。
  #
  # 注意：LVGL 每个字体文件都定义同名的内部符号（glyph_bitmap / cmaps 等），
  # **一个字库必须单独一个 .c 文件**，不能把两个字体 include 进同一个文件。
  nuttx_add_application(
    NAME
    velatime
    PRIORITY
    ${CONFIG_EXAMPLES_VELATIME_PRIORITY}
    STACKSIZE
    ${CONFIG_EXAMPLES_VELATIME_STACKSIZE}
    MODULE
    ${CONFIG_EXAMPLES_VELATIME}
    DEPENDS
    lvgl
    SRCS
    velatime_main.c
    core/core_task.c
    core/core_schedule.c
    core/core_recommend.c
    core/core_agent_sync.c
    ui/ui_theme.c
    ui/ui_mock.c
    ui/ui_home.c
    ui/ui_schedule.c
    ui/ui_tasks.c
    ui/velatime_font_hour56.c
    ui/velatime_font_time72.c
    ui/velatime_font_ui16.c
    ui/velatime_font_nav16.c
    ui/velatime_font_name20.c
    ui/velatime_font_meta15.c
    ui/velatime_font_cn16.c
    ui/velatime_font_body28.c
    ui/velatime_font_meta17.c
    ui/velatime_font_name22.c
    ui/velatime_font_name36.c
    ui/velatime_bg_moon.c
    ui/ui_popup.c)
endif()
