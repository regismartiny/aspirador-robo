#define FOREACH_COMMAND(COMMAND) \
        COMMAND(TURN_ON)   \
        COMMAND(TURN_OFF)  \
        COMMAND(END) /*used only to control the iteration loop*/

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

enum COMMAND_ENUM {
    FOREACH_COMMAND(GENERATE_ENUM)
};

static const char *COMMAND_STRING[] = {
    FOREACH_COMMAND(GENERATE_STRING)
};