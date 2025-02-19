template <int N>
DynLibBoard<N>::DynLibBoard (int board_id, struct BrainFlowInputParams params)
    : Board (board_id, params)
{
    is_streaming = false;
    keep_alive = false;
    initialized = false;
    state = (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR;
    dll_loader = NULL;
}

template <int N>
DynLibBoard<N>::~DynLibBoard ()
{
    skip_logs = true;
    release_session ();
}

template <int N>
int DynLibBoard<N>::prepare_session ()
{
    if (initialized)
    {
        safe_logger (spdlog::level::info, "Session is already prepared");
        return (int)BrainFlowExitCodes::STATUS_OK;
    }
    if (params.timeout <= 0)
    {
        params.timeout = 5;
    }

    dll_loader = new DLLLoader (get_lib_name ().c_str ());
    if (!dll_loader->load_library ())
    {
        safe_logger (spdlog::level::err, "Failed to load library");
        delete dll_loader;
        dll_loader = NULL;
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    safe_logger (spdlog::level::debug, "Library is loaded");
    int res = call_init ();
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        delete dll_loader;
        dll_loader = NULL;
        return res;
    }
    res = call_open ();
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        delete dll_loader;
        dll_loader = NULL;
        return res;
    }
    initialized = true;
    return (int)BrainFlowExitCodes::STATUS_OK;
}

template <int N>
int DynLibBoard<N>::start_stream (int buffer_size, char *streamer_params)
{
    if (is_streaming)
    {
        safe_logger (spdlog::level::err, "Streaming thread already running");
        return (int)BrainFlowExitCodes::STREAM_ALREADY_RUN_ERROR;
    }
    if (buffer_size <= 0 || buffer_size > MAX_CAPTURE_SAMPLES)
    {
        safe_logger (spdlog::level::err, "invalid array size");
        return (int)BrainFlowExitCodes::INVALID_BUFFER_SIZE_ERROR;
    }

    int res = prepare_for_acquisition (buffer_size, streamer_params);
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        return res;
    }

    res = call_start ();
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        return res;
    }

    keep_alive = true;
    streaming_thread = std::thread ([this] { read_thread (); });

    // wait for data to ensure that everything is okay
    std::unique_lock<std::mutex> lk (m);
    auto sec = std::chrono::seconds (1);
    if (cv.wait_for (lk, params.timeout * sec,
            [this] { return state != (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR; }))
    {
        is_streaming = true;
        return state;
    }
    else
    {
        safe_logger (
            spdlog::level::err, "no data received in {} sec, stopping thread", params.timeout);
        is_streaming = true;
        stop_stream ();
        return (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR;
    }
}

template <int N>
int DynLibBoard<N>::stop_stream ()
{
    if (is_streaming)
    {
        keep_alive = false;
        is_streaming = false;
        streaming_thread.join ();
        state = (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR;
        return call_stop ();
    }
    else
    {
        return (int)BrainFlowExitCodes::STREAM_THREAD_IS_NOT_RUNNING;
    }
}

template <int N>
int DynLibBoard<N>::release_session ()
{
    if (initialized)
    {
        stop_stream ();
        initialized = false;
    }

    free_packages ();

    call_close ();
    call_release ();

    if (dll_loader != NULL)
    {
        dll_loader->free_library ();
        delete dll_loader;
        dll_loader = NULL;
    }

    return (int)BrainFlowExitCodes::STATUS_OK;
}

template <int N>
void DynLibBoard<N>::read_thread ()
{
    int num_attempts = 0;
    int sleep_time = 10;
    int max_attempts = params.timeout * 1000 / sleep_time;

    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("get_data");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for get_data");
        return;
    }

    while (keep_alive)
    {
        double data[N];
        int res = func ((void *)&data);
        if (res == (int)BrainFlowExitCodes::STATUS_OK)
        {
            if (state != (int)BrainFlowExitCodes::STATUS_OK)
            {
                {
                    std::lock_guard<std::mutex> lk (m);
                    state = (int)BrainFlowExitCodes::STATUS_OK;
                }
                cv.notify_one ();
            }
            push_package (data);
        }
        else
        {
            if (state == (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR)
            {
                num_attempts++;
            }
            if (num_attempts == max_attempts)
            {
                safe_logger (spdlog::level::err, "no data received");
                {
                    std::lock_guard<std::mutex> lk (m);
                    state = (int)BrainFlowExitCodes::GENERAL_ERROR;
                }
                cv.notify_one ();
                break;
            }
#ifdef _WIN32
            Sleep (sleep_time);
#else
            usleep (sleep_time * 1000);
#endif
        }
    }
}

template <int N>
int DynLibBoard<N>::config_board (std::string config, std::string &response)
{
    return call_config ((char *)config.c_str ());
}

template <int N>
int DynLibBoard<N>::call_init ()
{
    if (dll_loader == NULL)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }
    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("initialize");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for initialize");
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }

    return func (NULL);
}

template <int N>
int DynLibBoard<N>::call_open ()
{
    if (dll_loader == NULL)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }
    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("open_device");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for open_device");
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return func (NULL);
}

template <int N>
int DynLibBoard<N>::call_start ()
{
    if (dll_loader == NULL)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }
    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("start_stream");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for start_stream");
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return func (NULL);
}

template <int N>
int DynLibBoard<N>::call_stop ()
{
    if (dll_loader == NULL)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }
    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("stop_stream");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for stop_stream");
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return func (NULL);
}

template <int N>
int DynLibBoard<N>::call_close ()
{
    if (dll_loader == NULL)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }
    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("close_device");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for close_device");
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return func (NULL);
}

template <int N>
int DynLibBoard<N>::call_release ()
{
    if (dll_loader == NULL)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }
    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("release");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for release");
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return func (NULL);
}

template <int N>
int DynLibBoard<N>::call_config (char *config)
{
    if (dll_loader == NULL)
    {
        return (int)BrainFlowExitCodes::BOARD_NOT_READY_ERROR;
    }
    int (*func) (void *) = (int (*) (void *))dll_loader->get_address ("config_device");
    if (func == NULL)
    {
        safe_logger (spdlog::level::err, "failed to get function address for release");
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return func ((void *)config);
}
