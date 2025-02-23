#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string.h>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "board.h"
#include "board_controller.h"
#include "runtime_dll_loader.h"

template <int N>
class DynLibBoard : public Board
{

protected:
    volatile bool keep_alive;
    bool initialized;
    bool is_streaming;
    std::thread streaming_thread;
    std::mutex m;
    std::condition_variable cv;
    volatile int state;
    DLLLoader *dll_loader;

    virtual int call_init ();
    virtual int call_open ();
    virtual int call_close ();
    virtual int call_start ();
    virtual int call_stop ();
    virtual int call_release ();
    virtual int call_config (char *config);

    virtual void read_thread ();
    virtual std::string get_lib_name () = 0;


public:
    DynLibBoard (int board_id, struct BrainFlowInputParams params);
    virtual ~DynLibBoard ();

    virtual int prepare_session ();
    virtual int start_stream (int buffer_size, char *streamer_params);
    virtual int stop_stream ();
    virtual int release_session ();
    virtual int config_board (std::string config, std::string &response);
};

#include "../dyn_lib_board.tpp"