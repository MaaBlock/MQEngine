/**
 *@file entry.h
 *@brief The declaration of entry point
 *@note The entry point has been unified in FCT library.
*/
#ifndef ENTRY_H
#define ENTRY_H
#ifdef FCT_ANDROID
int ENGINE_API main();
#else
int ENGINE_API main(int argc, char* argv[]);
#endif
#endif //ENTRY_H
