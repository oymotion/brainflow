classdef BoardIDs < int32
    % Store all supported board ids
    enumeration
        PLAYBACK_FILE_BOARD(-3)
        STREAMING_BOARD(-2)
        SYNTHETIC_BOARD(-1)
        CYTON_BOARD(0)
        GANGLION_BOARD(1)
        CYTON_DAISY_BOARD(2)
        GALEA_BOARD(3)
        GANGLION_WIFI(4)
        CYTON_WIFI(5)
        CYTON_DAISY_WIFI(6)
        BRAINBIT_BOARD(7)
        UNICORN_BOARD(8)
        CALLIBRI_EEG_BOARD(9)
        CALLIBRI_EMG_BOARD(10)
        CALLIBRI_ECG_BOARD(11)
        FASCIA_BOARD(12)
        NOTION_1_BOARD(13)
        NOTION_2_BOARD(14)
        IRONBCI_BOARD(15)
        GFORCE_PRO_BOARD(16)
        FREEEEG32_BOARD(17)
        BRAINBIT_BLED_BOARD(18)
    end
end