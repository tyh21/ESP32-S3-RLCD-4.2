// 提供跨任务共享的当前工作页原子读写接口。
#pragma once

int active_work_page_load();
void active_work_page_store(int page);
