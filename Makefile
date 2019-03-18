#
# Copyright (C) 2019 Alexander Amelkin <alexander@amelkin.msk.ru>
#

all: at45

at45: at45.c
	${CC} -o $@ $^
