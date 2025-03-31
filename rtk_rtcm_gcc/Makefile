# makefile for rtk
CC = gcc
BINDIR  = /usr/local/bin
SRC     = ../src

ifeq ($(OS), Windows_NT)
	OPTS    = -DENACMP -DWIN32
	LDLIBS  = -lm  -lwinmm -lpthread -lws2_32
else
	LDLIBS  = -lm -lpthread
	OPTS    = -DENACMP
endif

CFLAGS  = -Wall -O0 -g -std=c11 -pedantic -I$(SRC) -DLAPACK $(OPTS)

# for gprof
#CFLAGS  = -Wall -O3 -std=c11 -pedantic -I$(SRC) -DLAPACK $(OPTS) -pg
#LDLIBS  = -lm -lrt -llapack -lblas -pg

# for mkl
#MKLDIR  = /opt/intel
#CFLAGS  = -O3 -std=c11 -pedantic -I$(SRC) $(OPTS) -DMKL -I$(MKLDIR)/include
#LDLIBS  = -L$(MKLDIR)/lib/32 -lm -lmkl_core -lmkl_intel -lmkl_gnu_thread -liomp5 -lpthread

all	: main_rtcm 
main_rtcm : main_rtcm.o rtkcmn.o rinex.o  solution.o
main_rtcm : lambda.o myfun.o pntpos.o ephemeris.o 
main_rtcm : rtcm3.o rtkls.o rtkpos.o  tides.o stream.o multipath.o cJSON.o BandPassFilter.o fft.o
main_rtcm : logmod.o pos_filter.o

main_rtcm.o : $(SRC)/main_rtcm.c
	$(CC) -c $(CFLAGS) $(SRC)/main_rtcm.c
rtkcmn.o   : $(SRC)/rtkcmn.c
	$(CC) -c $(CFLAGS) $(SRC)/rtkcmn.c
rinex.o    : $(SRC)/rinex.c
	$(CC) -c $(CFLAGS) $(SRC)/rinex.c
solution.o : $(SRC)/solution.c
	$(CC) -c $(CFLAGS) $(SRC)/solution.c
lambda.o   : $(SRC)/lambda.c
	$(CC) -c $(CFLAGS) $(SRC)/lambda.c
myfun.o    : $(SRC)/myfun.c
	$(CC) -c $(CFLAGS) $(SRC)/myfun.c
pntpos.o   : $(SRC)/pntpos.c
	$(CC) -c $(CFLAGS) $(SRC)/pntpos.c
ephemeris.o: $(SRC)/ephemeris.c
	$(CC) -c $(CFLAGS) $(SRC)/ephemeris.c
rtkpos.o  : $(SRC)/rtkpos.c
	$(CC) -c $(CFLAGS) $(SRC)/rtkpos.c
rtcm3.o    : $(SRC)/rtcm3.c
	$(CC) -c $(CFLAGS) $(SRC)/rtcm3.c
rtkls.o     : $(SRC)/rtkls.c
	$(CC) -c $(CFLAGS) $(SRC)/rtkls.c
tides.o     : $(SRC)/tides.c
	$(CC) -c $(CFLAGS) $(SRC)/tides.c
stream.o     : $(SRC)/stream.c
	$(CC) -c $(CFLAGS) $(SRC)/stream.c

multipath.o     : $(SRC)/multipath.c
	$(CC) -c $(CFLAGS) $(SRC)/multipath.c

cJSON.o     : $(SRC)/cJSON.c
	$(CC) -c $(CFLAGS) $(SRC)/cJSON.c

fft.o     : $(SRC)/fft.c
	$(CC) -c $(CFLAGS) $(SRC)/fft.c

BandPassFilter.o     : $(SRC)/BandPassFilter.c
	$(CC) -c $(CFLAGS) $(SRC)/BandPassFilter.c

logmod.o     : $(SRC)/logmod.c
	$(CC) -c $(CFLAGS) $(SRC)/logmod.c
pos_filter.o     : $(SRC)/pos_filter.c
	$(CC) -c $(CFLAGS) $(SRC)/pos_filter.c


main_rtcm.o 	: $(SRC)/rtk.h
rtkcmn.o    : $(SRC)/rtk.h
rinex.o     : $(SRC)/rtk.h
rtkpos.o    : $(SRC)/rtk.h
solution.o  : $(SRC)/rtk.h
lambda.o    : $(SRC)/rtk.h
myfun.o     : $(SRC)/rtk.h
sbas.o      : $(SRC)/rtk.h
preceph.o   : $(SRC)/rtk.h
pntpos.o    : $(SRC)/rtk.h
ephemeris.o : $(SRC)/rtk.h
options.o   : $(SRC)/rtk.h
ppp.o       : $(SRC)/rtk.h
ppp_ar.o    : $(SRC)/rtk.h
rtcm.o      : $(SRC)/rtk.h
rtcm2.o     : $(SRC)/rtk.h
rtcm3.o     : $(SRC)/rtk.h
rtcm3e.o    : $(SRC)/rtk.h
rtkls.o     : $(SRC)/rtk.h
tides.o		: $(SRC)/rtk.h
stream.o	: $(SRC)/rtk.h
multipath.o : $(SRC)/rtk.h
cJSON.o 	: $(SRC)/cJSON.h
fft.o 		: $(SRC)/fft.h
BandPassFilter.o :$(SRC)/BandPassFilter.h
logmod.o :$(SRC)/logmod.h
pos_filter.o :$(SRC)/pos_filter.h


clean:
	rm -rf *.o
