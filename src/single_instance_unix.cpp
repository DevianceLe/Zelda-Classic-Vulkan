#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstddef>
#include <cstring>

#include "single_instance.h"

bool is_only_instance(const char* name)
{
    int fd;
    sockaddr_un sun;
    socklen_t length;

    fd=socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd==-1)
        return false;

    // sun_path is 108 bytes on Linux but 104 on macOS (which also has sun_len before
    // sun_family): the old fixed 108 made strncpy write past it there, and the address
    // length came out a byte short. Both are the same as before on Linux.
    memset(&sun, 0, sizeof(sun));
    sun.sun_family=AF_UNIX;
    strncpy(sun.sun_path, name, sizeof(sun.sun_path) - 1);
    length=(socklen_t)(offsetof(sockaddr_un, sun_path) + strlen(sun.sun_path));

    if(connect(fd, (sockaddr*)&sun, length)!=-1)
        // Connect succeeded; there must be another instance running already
        return false;

    unlink(sun.sun_path);
    if(bind(fd, (sockaddr*)&sun, length)==-1)
        return false;
    if(listen(fd, 5)==-1)
        return false;

    return true;
}
