#ifndef INPUT_DATA_H
#define INPUT_DATA_H

/** file contains sample keybidnings in order to test our input **/
#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

extern vector_t kp_q;
extern vector_t kp_of;
extern vector_t kp_cf;
extern vector_t kp_lb;
extern vector_t kp_bl;
extern vector_t kp_ro;
extern vector_t kp_audioup;
extern vector_t kp_audiodw;
extern vector_t kp_audiopl;
extern vector_t kp_audiops;
extern vector_t kp_audionx;
extern vector_t kp_audiopv;


void func_quit(void);
void func_of(void);
void func_cf(void);
void func_lb(void);
void func_bl(void);
void func_ro(void);
void func_audioup(void);
void func_audiodw(void);
void func_audio(void);




#ifdef __cplusplsu
}
#endif


#endif /* EOF */
