#ifndef CTYPE_H
#define CTYPE_H
extern int tolower(int c);
extern int toupper(int c);
static inline int isspace(int c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'); }
static inline int isdigit(int c) { return (c >= '0' && c <= '9'); }
static inline int isxdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isprint(int c) { return (c >= 0x20 && c <= 0x7e); }
#endif
