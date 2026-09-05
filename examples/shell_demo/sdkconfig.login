# Overlay: start the shell demo locked behind `login <password>`.
# Appended to SDKCONFIG_DEFAULTS by CMakeLists.txt when -DLOGIN=y is given.
CONFIG_ZSHELL_START_OBSCURED=y
CONFIG_ZSHELL_CMD_ROOT="login"
