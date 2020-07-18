#include <syslog.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef UTILS_H_
#define UTILS_H_

#define LOG_DEBUG_SERIAL 8

#ifndef EXIT_SUCCESS
  #define EXIT_FAILURE 1
  #define EXIT_SUCCESS 0
#endif

#ifndef TRUE
  #define TRUE 1
  #define FALSE 0
#endif

#define MAXLEN 256

#define round(a) (int) (a+0.5) // 0 decimal places
#define roundf(a) (float) ((a*100)/100) // 2 decimal places

// Defined as int16_t so 16 bits to mask
#define AQUA_LOG (1 << 0) // Aqualink Generic / catchall
#define NET_LOG  (1 << 1) // Network
// Control protocols
#define AQRS_LOG (1 << 2) // Allbutton RS Keypad
#define ONET_LOG (1 << 3) // OneTouch
#define IAQT_LOG (1 << 4) // iAqualinkTouch
#define PDA_LOG  (1 << 5) // PDA
// Message PRotocols
#define DJAN_LOG (1 << 6) // Jange Device
#define DPEN_LOG (1 << 7) // Pentair Device
// misc
#define RSSD_LOG (1 << 8) // Serial
/*
typedef enum
{
  false = FALSE, true = TRUE
} bool;
*/
//void setLoggingPrms(int level , bool deamonized, char* log_file);
void setLoggingPrms(int level , bool deamonized, char* log_file, char *error_messages);
int getLogLevel(int16_t from);
void daemonise ( char *pidFile, void (*main_function)(void) );
//void debugPrint (char *format, ...);
void displayLastSystemError (const char *on_what);


void addDebugLogMask(int16_t flag);
//#define logMessage(msg_level, format, ...) LOG (1, msg_level, format, ##__VA_ARGS__)
void logMessage(int level, const char *format, ...);
//void LOG(int from, int level, char *format, ...);
void LOG(int16_t from, int msg_level, const char *format, ...);

int count_characters(const char *str, char character);
//void readCfg (char *cfgFile);
int text2elevel(char* level);
char *elevel2text(int level);
char *cleanwhitespace(char *str);
//char *cleanquotes(char *str);
char *chopwhitespace(char *str);
char *trimwhitespace(char *str);
char *stripwhitespace(char *str);
int cleanint(char*str);
bool text2bool(char *str);
bool request2bool(char *str);
char *bool2text(bool val);
void delay (unsigned int howLong);
float degFtoC(float degF);
float degCtoF(float degC);
char* stristr(const char* haystack, const char* needle);
//int ascii(char *destination, char *source);
char *prittyString(char *str);
//void writePacketLog(char *buff);
//void closePacketLog();
void startInlineDebug();
void startInlineSerialDebug();
void stopInlineDebug();
void cleanInlineDebug();
char *getInlineLogFName();
bool islogFileReady();

//#ifndef _UTILS_C_
  extern bool _daemon_;
  extern bool _debuglog_;
  extern bool _debug2file_;
//#endif

#endif /* UTILS_H_ */
