# main "ve" automaticamente todos los componentes de components/, no hace falta REQUIRES
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")
