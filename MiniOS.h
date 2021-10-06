// Coral Rubilar 316392877
// Moriel Turjeman 308354968


char *taskGetName(int id);
char* taskGetMem(int id);
void taskReleaseMem(int id);
void taskSetMem(int id, char* buffer);
bool taskShouldSuspend(int id);
void taskWait(int id, int time);
void taskSuspend(int id);
