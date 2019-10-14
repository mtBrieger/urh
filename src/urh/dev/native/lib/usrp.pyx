import time

from urh.dev.native.lib.cusrp cimport *
import numpy as np
# noinspection PyUnresolvedReferences
cimport numpy as np
from libc.stdlib cimport malloc, free, calloc
# noinspection PyUnresolvedReferences
from cython.view cimport array as cvarray  # needed for converting of malloc array to python array

import cython
from libc.string cimport memcpy

from threading import Thread

from ctypes import *

from libc.stdio cimport *

import os
import stat
import subprocess
import time


cdef uhd_usrp_handle _c_device
cdef uhd_rx_streamer_handle rx_streamer_handle
cdef uhd_tx_streamer_handle tx_streamer_handle
cdef uhd_rx_metadata_handle rx_metadata_handle
cdef uhd_tx_metadata_handle tx_metadata_handle

cpdef bint IS_TX = False
cpdef size_t CHANNEL = 0
cdef size_t max_num_rx_samples = 2040
cdef size_t max_num_tx_samples = 2040

cdef char* tx_fifo = "/tmp/tx_fifo"
cdef char* rx_fifo = "/tmp/rx_fifo"


cpdef set_tx(bint is_tx):
    global IS_TX
    IS_TX = is_tx

cpdef set_channel(size_t channel):
    global CHANNEL
    CHANNEL = channel

cpdef uhd_error open(str device_args):
    print('open')
    return UHD_ERROR_NONE

cpdef uhd_error close():
    print('close')
    return UHD_ERROR_NONE

cpdef uhd_error setup_stream():
    print("setup_stream")
    return UHD_ERROR_NONE

def run_script(double center_freq):
    print("run_script")
    return subprocess.Popen(['txrx_fifo', '--tx-freq', str(int(center_freq)), '--rx-freq', str(int(center_freq))])

cpdef uhd_error start_stream(int num_samples):

    print("waiting for device")
    while not os.path.exists(rx_fifo):
        pass

    print("start_stream")

    if not IS_TX:
        time.sleep(0.5)

    return UHD_ERROR_NONE


cpdef uhd_error stop_stream():
    print("stop_stream")
    return UHD_ERROR_NONE

cpdef uhd_error destroy_stream():
    print("destroy_stream")
    #if IS_TX:
    os.remove(tx_fifo)
    #else:
    os.remove(rx_fifo)

    return UHD_ERROR_NONE

cpdef uhd_error recv_stream_async(connection, int num_samples):
    print("recv_stream_async")
    Thread(target=recv_stream_loop, args=(connection,num_samples,)).start()


cpdef uhd_error recv_stream_loop(connection, int num_samples):
    print("recv_stream_loop")
    while True:
        recv_stream(connection, num_samples)

cpdef uhd_error recv_stream(connection, int num_samples):
    num_samples = (<int>(num_samples / max_num_rx_samples) + 1) * max_num_rx_samples
    cdef float* result = <float*>calloc(sizeof(float), 2*num_samples)
    if not result:
        raise MemoryError()

    cdef FILE *f = fopen(rx_fifo, "r")

    # complex float => 2*num_samples
    samps = fread(result, sizeof(float), 2*num_samples, f)
    connection.send_bytes(<float[:samps]>result)

    fclose(f)
    free(result)






@cython.boundscheck(False)
@cython.initializedcheck(False)
@cython.wraparound(False)
cpdef uhd_error send_stream(float[::1] samples):
    if len(samples) == 1 and samples[0] == 0:
        # Fill with zeros. Use some more zeros to prevent underflows
        samples = np.zeros(8 * max_num_tx_samples, dtype=np.float32)

    cdef unsigned long i, index = 0
    cdef size_t num_samps_sent = 0
    cdef size_t sample_count = len(samples)

    cdef float* buff = <float *>malloc(max_num_tx_samples * 2 * sizeof(float))
    if not buff:
        raise MemoryError()

    cdef FILE *f;

    for i in range(0, sample_count):
        buff[index] = samples[i]

        index += 1
        if index >= 2*max_num_tx_samples:
            index = 0

            f = fopen(tx_fifo, "w")
            # complex float => 2*max_num_tx_samples
            fwrite(buff, sizeof(float), 2*max_num_tx_samples, f)
            fclose(f)

    f = fopen(tx_fifo, "w")
    fwrite(buff, sizeof(float), index, f)
    fclose(f)
    free(buff)


cpdef str get_device_representation():
    print("get_device_representation")
    return ""

cpdef uhd_error set_sample_rate(double sample_rate):
    print("set_sample_rate")
    return UHD_ERROR_NONE

cpdef uhd_error set_bandwidth(double bandwidth):
    print("set_bandwidth")
    return UHD_ERROR_NONE

cpdef uhd_error set_rf_gain(double normalized_gain):
    print("set_rf_gain")
    return UHD_ERROR_NONE


cpdef uhd_error set_center_freq(double center_freq):
    print("set_center_freq")

    run_script(center_freq)
    time.sleep(0.25)

    return UHD_ERROR_NONE

cpdef str get_last_error():
    print("get_last_error")
    return ""

cpdef list find_devices(str args):
    print("find_devices")
    return []
