#include "cyton_daisy.h"
#include "custom_cast.h"
#include "serial.h"
#include "timestamp.h"

#define START_BYTE 0xA0
#define END_BYTE_STANDARD 0xC0
#define END_BYTE_ANALOG 0xC1
#define END_BYTE_MAX 0xC6


void CytonDaisy::read_thread ()
{
    // format is the same as for cyton but need to join two packages together
    /*
        Byte 1: 0xA0
        Byte 2: Sample Number
        Bytes 3-5: Data value for EEG channel 1
        Bytes 6-8: Data value for EEG channel 2
        Bytes 9-11: Data value for EEG channel 3
        Bytes 12-14: Data value for EEG channel 4
        Bytes 15-17: Data value for EEG channel 5
        Bytes 18-20: Data value for EEG channel 6
        Bytes 21-23: Data value for EEG channel 6
        Bytes 24-26: Data value for EEG channel 8
        Aux Data Bytes 27-32: 6 bytes of data
        Byte 33: 0xCX where X is 0-F in hex
    */
    int res;
    unsigned char b[32];
    double package[30] = {0.};
    bool first_sample = false;
    while (keep_alive)
    {
        // check start byte
        res = serial->read_from_serial_port (b, 1);
        if (res != 1)
        {
            safe_logger (spdlog::level::debug, "unable to read 1 byte");
            continue;
        }
        if (b[0] != START_BYTE)
        {
            continue;
        }

        res = serial->read_from_serial_port (b, 32);
        if (res != 32)
        {
            safe_logger (spdlog::level::debug, "unable to read 31 bytes");
            continue;
        }
        if ((b[31] < END_BYTE_STANDARD) || (b[31] > END_BYTE_MAX))
        {
            safe_logger (spdlog::level::warn, "Wrong end byte {}", b[31]);
            continue;
        }

        // place unprocessed bytes to other_channels for all modes
        if ((b[0] % 2 == 0) && (first_sample))
        {
            // eeg
            for (int i = 0; i < 8; i++)
            {
                package[i + 9] = eeg_scale * cast_24bit_to_int32 (b + 1 + 3 * i);
            }
            // need to average other_channels
            package[21] += (double)b[25];
            package[22] += (double)b[26];
            package[23] += (double)b[27];
            package[24] += (double)b[28];
            package[25] += (double)b[29];
            package[26] += (double)b[30];
            package[21] /= 2.0;
            package[22] /= 2.0;
            package[23] /= 2.0;
            package[24] /= 2.0;
            package[25] /= 2.0;
            package[26] /= 2.0;
            package[20] = (double)b[31];
        }
        else
        {
            first_sample = true;
            package[0] = (double)b[0];
            // eeg
            for (int i = 0; i < 8; i++)
            {
                package[i + 1] = eeg_scale * cast_24bit_to_int32 (b + 1 + 3 * i);
            }
            // other_channels
            package[21] = (double)b[25];
            package[22] = (double)b[26];
            package[23] = (double)b[27];
            package[24] = (double)b[28];
            package[25] = (double)b[29];
            package[26] = (double)b[30];
        }

        // place processed accel data
        if (b[31] == END_BYTE_STANDARD)
        {
            if ((b[0] % 2 == 0) && (first_sample))
            {
                // need to average accel data
                package[17] += accel_scale * cast_16bit_to_int32 (b + 25);
                package[18] += accel_scale * cast_16bit_to_int32 (b + 27);
                package[19] += accel_scale * cast_16bit_to_int32 (b + 29);
                package[17] /= 2.0f;
                package[18] /= 2.0f;
                package[19] /= 2.0f;
                package[20] = (double)b[31];
            }
            else
            {
                first_sample = true;
                package[0] = (double)b[0];
                // accel
                package[17] = accel_scale * cast_16bit_to_int32 (b + 25);
                package[18] = accel_scale * cast_16bit_to_int32 (b + 27);
                package[19] = accel_scale * cast_16bit_to_int32 (b + 29);
            }
        }
        // place processed analog data
        if (b[31] == END_BYTE_ANALOG)
        {
            if ((b[0] % 2 == 0) && (first_sample))
            {
                // need to average analog data
                package[27] += cast_16bit_to_int32 (b + 25);
                package[28] += cast_16bit_to_int32 (b + 27);
                package[29] += cast_16bit_to_int32 (b + 29);
                package[27] /= 2.0f;
                package[28] /= 2.0f;
                package[29] /= 2.0f;
                package[20] = (double)b[31]; // cyton end byte
            }
            else
            {
                first_sample = true;
                package[0] = (double)b[0];
                // analog
                package[27] = cast_16bit_to_int32 (b + 25);
                package[28] = cast_16bit_to_int32 (b + 27);
                package[29] = cast_16bit_to_int32 (b + 29);
            }
        }
        // commit package
        if ((b[0] % 2 == 0) && (first_sample))
        {
            double timestamp = get_timestamp ();
            db->add_data (timestamp, package);
            streamer->stream_data (package, 30, timestamp);
        }
    }
}