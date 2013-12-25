#include <jni.h>

int osmosim_init(); 
int osmosim_loglevel(int log_level);
int osmosim_powerup();
void osmosim_powerdown();
int osmosim_reset();
int osmosim_transmit(char* data, unsigned int len, char** out);
void osmosim_exit();
