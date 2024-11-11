
#ifndef _SETTINGS_H_
#define _SETTINGS_H_


#define DEFAULT_PORT (42069)


#define MAXCLIENTS (10)


#define UNAMELEN (16)


#define MAXMSGLEN (256)


#define BROADCASTLEN (MAXMSGLEN + UNAMELEN + 32)


#define RSAKEYENC (62)


#define BITSPERDIG (6)


#define RSAKEYLEN (1024)


#define NUMCHUNKS (4)


#define RECVBUFFLEN (NUMCHUNKS * (RSAKEYLEN / BITSPERDIG))

#endif

