#
# Main Makefile. This is basically the same as a component makefile.
#
ifdef CONFIG_AUDIO_BOARD_CUSTOM
COMPONENT_ADD_INCLUDEDIRS += ./components/my_board
COMPONENT_SRCDIRS += ./components/my_board
endif