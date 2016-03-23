#ifndef __INTELI_ENG__
#define __INTELI_ENG__

extern int init_engine();
extern void release_engine();
extern void *get_fsm();
extern void release_fsm(void *resid);
extern int match_fsm(void *resid,unsigned char *text,int textlen);
extern int get_result(void *resid);
#endif
