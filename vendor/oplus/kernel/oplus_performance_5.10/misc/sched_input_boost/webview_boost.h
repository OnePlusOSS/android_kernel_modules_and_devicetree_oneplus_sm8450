#ifndef _WEBVIEW_BOOST__H
#define _WEBVIEW_BOOST__H
void task_rename_hook(void *unused, struct task_struct *tsk, const char *buf);
int find_webview_cpu(struct task_struct *p);
bool is_webview_boost(struct task_struct *p);
#endif
