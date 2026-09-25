# Overlay: start the shell demo locked; the password alone unlocks at `login: `.
# Appended to SDKCONFIG_DEFAULTS by CMakeLists.txt when -DLOGIN=y is given.
CONFIG_ZSHELL_START_OBSCURED=y
CONFIG_ZSHELL_CMD_ROOT="login"
