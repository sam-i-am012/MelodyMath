idf_component_register(SRCS "melody_math.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver
                       REQUIRES VL53L0X)
