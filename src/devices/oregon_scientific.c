/** @file
    Various Oregon Scientific protocols.

    Copyright (C) 2015 Helge Weissig, Denis Bodor, Tommy Vestermark, Karl Lattimer,
    deennoo, pclov3r, onlinux, Pasquale Fiorillo.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/// Documentation for Oregon Scientific protocols can be found here:
/// http://wmrx00.sourceforge.net/Arduino/OregonScientific-RF-Protocols.pdf
// Sensors ID
#define ID_THGR122N 0x1d20
#define ID_THGR968  0x1d30
#define ID_BHTR968  0x5d60
#define ID_RGR968   0x2d10
#define ID_THR228N  0xec40
#define ID_THN132N  0xec40 // same as THR228N but different packet size
#define ID_RTGN318  0x0cc3 // warning: id is from 0x0cc3 and 0xfcc3
#define ID_RTGN129  0x0cc3 // same as RTGN318 but different packet size
#define ID_THGR810  0xf824
#define ID_THN802   0xc844
#define ID_PCR800   0x2914
#define ID_PCR800a  0x2d14 // Different PCR800 ID - AU version I think
#define ID_THGR81   0xf824
#define ID_WGR800   0x1984
#define ID_WGR968   0x3d00
#define ID_UV800    0xd874
#define ID_THN129   0xcc43 // THN129 Temp only
#define ID_RTHN129  0x0cd3 // RTHN129 Temp, clock sensors
#define ID_BTHGN129 0x5d53 // Baro, Temp, Hygro sensor
#define ID_UVR128   0xec70

static float get_os_temperature(unsigned char *message)
{
    float temp_c = 0;
    temp_c = (((message[5] >> 4) * 100) + ((message[4] & 0x0f) * 10) + ((message[4] >> 4) & 0x0f)) / 10.0F;
    if (message[5] & 0x0f)
        temp_c = -temp_c;
    return temp_c;
}

static float get_os_rain_rate(unsigned char *message)
{
    float rain_rate = 0; // Nibbles 11..8 rain rate, LSD = 0.01 inches per hour
    rain_rate = (((message[5] & 0x0f) * 1000) + ((message[5] >> 4) * 100) + ((message[4] & 0x0f) * 10) + ((message[4] >> 4) & 0x0f)) / 100.0F;
    return rain_rate;
}

static float get_os_total_rain(unsigned char *message)
{
    float total_rain = 0.0F; // Nibbles 17..12 Total rain, LSD = 0.001, 543210 = 012.345 inches
    total_rain = (message[8] & 0x0f) * 100.0F
            + ((message[8] >> 4) & 0x0f) * 10.0F + (message[7] & 0x0f)
            + ((message[7] >> 4) & 0x0f) / 10.0F + (message[6] & 0x0f) / 100.0F
            + ((message[6] >> 4) & 0x0f) / 1000.0F;
    return total_rain;
}

static unsigned int get_os_humidity(unsigned char *message)
{
    int humidity = 0;
    humidity = ((message[6] & 0x0f) * 10) + (message[6] >> 4);
    return humidity;
}

static unsigned int get_os_uv(unsigned char *message)
{
    int uvidx = 0;
    uvidx = ((message[4] & 0x0f) * 10) + (message[4] >> 4);
    return uvidx;
}

static unsigned int get_os_channel(unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included to support sensors with channel in different position
    int channel = 0;
    channel = ((message[2] >> 4) & 0x0f);
    if ((channel == 4) && (sensor_id & 0x0fff) != ID_RTGN318 && sensor_id != ID_THGR810 && (sensor_id & 0x0fff) != ID_RTHN129)
        channel = 3; // sensor 3 channel number is 0x04
    return channel;
}

static unsigned int get_os_battery(unsigned char *message)
{
    int battery_low = 0;
    battery_low = (message[3] >> 2 & 0x01);
    return battery_low;
}

static unsigned int get_os_rollingcode(unsigned char *message)
{
    int rc = 0;
    rc = (message[2] & 0x0F) + (message[3] & 0xF0);
    return rc;
}

static unsigned short int power(uint8_t const *msg)
{
    unsigned short int val = 0;
    val = (msg[4] << 8) | (msg[3] & 0xF0);
    val *= 1.00188;
    return val;
}

static unsigned long long total(uint8_t const *msg)
{
    unsigned long long val = 0;
    if ((msg[1] & 0x0F) == 0) {
        // Sensor returns total only if nibble#4 == 0
        val = (unsigned long long)msg[10] << 40;
        val += (unsigned long long)msg[9] << 32;
        val += (unsigned long)msg[8] << 24;
        val += (unsigned long)msg[7] << 16;
        val += msg[6] << 8;
        val += msg[5];
    }
    return val;
}

static int validate_os_checksum(r_device *decoder, unsigned char *msg, int checksum_nibble_idx)
{
    // Oregon Scientific v2.1 and v3 checksum is a    1 byte    'sum of nibbles' checksum.
    // with the 2 nibbles of the checksum byte    swapped.
    int i;
    unsigned int checksum, sum_of_nibbles = 0;
    for (i = 0; i < checksum_nibble_idx - 1; i += 2) {
        unsigned char val = msg[i >> 1];
        sum_of_nibbles += ((val >> 4) + (val & 0x0f));
    }
    if (checksum_nibble_idx & 1) {
        sum_of_nibbles += (msg[checksum_nibble_idx >> 1] >> 4);
        checksum = (msg[checksum_nibble_idx >> 1] & 0x0f) | (msg[(checksum_nibble_idx + 1) >> 1] & 0xf0);
    }
    else {
        checksum = (msg[checksum_nibble_idx >> 1] >> 4) | ((msg[checksum_nibble_idx >> 1] & 0x0f) << 4);
    }
    sum_of_nibbles &= 0xff;

    if (sum_of_nibbles == checksum) {
        return 0;
    }
    else {
        if (decoder->verbose) {
            fprintf(stderr, "Checksum error in Oregon Scientific message.    Expected: %02x    Calculated: %02x\n", checksum, sum_of_nibbles);
            bitrow_printf(msg, ((checksum_nibble_idx + 4) >> 1) * 8, "Message: ");
        }
        return 1;
    }
}

static int validate_os_v2_message(r_device *decoder, unsigned char *msg, int bits_expected, int valid_v2_bits_received,
        int nibbles_in_checksum)
{
    // Oregon scientific v2.1 protocol sends each bit using the complement of the bit, then the bit    for better error checking.    Compare number of valid bits processed vs number expected
    if (bits_expected == valid_v2_bits_received) {
        return (validate_os_checksum(decoder, msg, nibbles_in_checksum));
    }
    else {
        if (decoder->verbose) {
            fprintf(stderr, "Bit validation error on Oregon Scientific message.    Expected %d bits, received error after bit %d \n",                bits_expected, valid_v2_bits_received);
            bitrow_printf(msg, bits_expected, "Message: ");
        }
        return 1;
    }
}

static int oregon_scientific_v2_1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];
    data_t *data;

    // Check 2nd and 3rd bytes of stream for possible Oregon Scientific v2.1 sensor data (skip first byte to get past sync/startup bit errors)
    if (((b[1] != 0x55) || (b[2] != 0x55))
            && ((b[1] != 0xAA) || (b[2] != 0xAA))) {
        if (b[3] != 0) {
            if (decoder->verbose)
                bitrow_printf(b, bitbuffer->bits_per_row[0], "Badly formatted OS v2.1 message: ");
        }
        return 0;
    }

    bitbuffer_t databits = {0};
    uint8_t *msg = databits.bb[0];

    // Possible    v2.1 Protocol message
    int num_valid_v2_bits = 0;

    unsigned int sync_test_val = ((unsigned)b[3] << 24) | (b[4] << 16) | (b[5] << 8) | (b[6]);
    // Could be extra/dropped bits in stream.    Look for sync byte at expected position +/- some bits in either direction
    for (int pattern_index = 0; pattern_index < 8; pattern_index++) {
        unsigned int mask     = (unsigned int)(0xffff0000 >> pattern_index);
        unsigned int pattern  = (unsigned int)(0x55990000 >> pattern_index);
        unsigned int pattern2 = (unsigned int)(0xaa990000 >> pattern_index);

        if (decoder->verbose) {
            fprintf(stderr, "OS v2.1 sync byte search - test_val=%08x pattern=%08x    mask=%08x\n", sync_test_val, pattern, mask);
        }

        if (((sync_test_val & mask) != pattern)
                && ((sync_test_val & mask) != pattern2))
            continue;

        // Found sync byte - start working on decoding the stream data.
        // pattern_index indicates    where sync nibble starts, so now we can find the start of the payload
        if (decoder->verbose) {
            fprintf(stderr, "OS v2.1 Sync test val %08x found, starting decode at bit %d\n", sync_test_val, pattern_index);
        }

        //bitrow_printf(b, bitbuffer->bits_per_row[0], "Raw OSv2 bits: ");
        bitbuffer_manchester_decode(bitbuffer, 0, pattern_index + 40, &databits, 160);
        reflect_nibbles(databits.bb[0], (databits.bits_per_row[0]+7)/8);
        //bitbuffer_printf(&databits, "MC OSv2 bits (from %d+40): ", pattern_index);
        num_valid_v2_bits = databits.bits_per_row[0] * 2 + 1;

        break;
    }

    int sensor_id = (msg[0] << 8) | msg[1];
    if ((sensor_id == ID_THGR122N) || (sensor_id == ID_THGR968)) {
        if (validate_os_v2_message(decoder, msg, 153, num_valid_v2_bits, 15) != 0)
            return 0;
        data = data_make(
                "brand",                 "",                        DATA_STRING, "OS",
                "model",                 "",                        DATA_STRING, (sensor_id == ID_THGR122N) ? _X("Oregon-THGR122N","THGR122N"): _X("Oregon-THGR968","THGR968"),
                "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg),
                "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",             "Battery",         DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_os_temperature(msg),
                "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_WGR968) {
        if (validate_os_v2_message(decoder, msg, 189, num_valid_v2_bits, 17) != 0)
            return 0;
        float quadrant      = (msg[4] & 0x0f) * 10 + ((msg[4] >> 4) & 0x0f) * 1 + ((msg[5] >> 4) & 0x0f) * 100;
        float avgWindspeed  = ((msg[7] >> 4) & 0x0f) / 10.0F + (msg[7] & 0x0f) * 1.0F + ((msg[8] >> 4) & 0x0f) / 10.0F;
        float gustWindspeed = (msg[5] & 0x0f) / 10.0F + ((msg[6] >> 4) & 0x0f) * 1.0F + (msg[6] & 0x0f) / 10.0F;
        data = data_make(
                "brand",            "",                     DATA_STRING, "OS",
                "model",            "",                     DATA_STRING, _X("Oregon-WGR968","WGR968"),
                "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg),
                "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",        "Battery",        DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                _X("wind_max_m_s","gust"), "Gust",             DATA_FORMAT, "%2.1f m/s",DATA_DOUBLE, gustWindspeed,
                _X("wind_avg_m_s","average"), "Average",        DATA_FORMAT, "%2.1f m/s",DATA_DOUBLE, avgWindspeed,
                _X("wind_dir_deg","direction"),    "Direction",    DATA_FORMAT, "%3.1f degrees",DATA_DOUBLE, quadrant,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_BHTR968) {
        if (validate_os_v2_message(decoder, msg, 185, num_valid_v2_bits, 19) != 0)
            return 0;
        unsigned int comfort = msg[7] >> 4;
        char *comfort_str = "Normal";
        if (comfort == 4) comfort_str = "Comfortable";
        else if (comfort == 8) comfort_str = "Dry";
        else if (comfort == 0xc) comfort_str = "Humid";
        unsigned int forecast = msg[9] >> 4;
        char *forecast_str = "Cloudy";
        if (forecast == 3) forecast_str = "Rainy";
        else if (forecast == 6) forecast_str = "Partly Cloudy";
        else if (forecast == 0xc) forecast_str = "Sunny";
        float temp_c = get_os_temperature(msg);
        float pressure = ((msg[7] & 0x0f) | (msg[8] & 0xf0)) + 856;
        // fprintf(stderr,"Weather Sensor BHTR968    Indoor        Temp: %3.1fC    %3.1fF     Humidity: %d%%", temp_c, ((temp_c*9)/5)+32, get_os_humidity(msg));
        // fprintf(stderr, " (%s) Pressure: %dmbar (%s)\n", comfort_str, ((msg[7] & 0x0f) | (msg[8] & 0xf0))+856, forecast_str);
        data = data_make(
                "brand",            "",                             DATA_STRING, "OS",
                "model",            "",                             DATA_STRING, _X("Oregon-BHTR968","BHTR968"),
                "id",                 "House Code",         DATA_INT,        get_os_rollingcode(msg),
                "channel",        "Channel",                DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",        "Battery",                DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",     "Humidity",             DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                "pressure_hPa",    "Pressure",        DATA_FORMAT, "%.0f hPa",     DATA_DOUBLE, pressure,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_RGR968) {
        if (validate_os_v2_message(decoder, msg, 161, num_valid_v2_bits, 16) != 0)
            return 0;
        float rain_rate  = ((msg[4] & 0x0f) * 100 + (msg[4] >> 4) * 10 + ((msg[5] >> 4) & 0x0f)) / 10.0F;
        float total_rain = ((msg[7] & 0xf) * 10000 + (msg[7] >> 4) * 1000 + (msg[6] & 0xf) * 100 + (msg[6] >> 4) * 10 + (msg[5] & 0xf)) / 10.0F;
        data = data_make(
                "brand",            "",                     DATA_STRING, "OS",
                "model",            "",                     DATA_STRING, _X("Oregon-RGR968","RGR968"),
                "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg),
                "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",        "Battery",        DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                _X("rain_rate_mm_h","rain_rate"),    "Rain Rate",    DATA_FORMAT, "%.02f mm/hr", DATA_DOUBLE, rain_rate,
                _X("rain_mm","total_rain"), "Total Rain", DATA_FORMAT, "%.02f mm", DATA_DOUBLE, total_rain,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_THR228N && num_valid_v2_bits == 153) {
        if (validate_os_v2_message(decoder, msg, 153, num_valid_v2_bits, 12) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        data = data_make(
                "brand",                 "",                        DATA_STRING, "OS",
                "model",                 "",                        DATA_STRING, _X("Oregon-THR228N","THR228N"),
                "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg),
                "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",             "Battery",         DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_THN132N && num_valid_v2_bits == 129) {
        if (validate_os_v2_message(decoder, msg, 129, num_valid_v2_bits, 12) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        data = data_make(
                "brand",                 "",                        DATA_STRING, "OS",
                "model",                 "",                        DATA_STRING, _X("Oregon-THN132N","THN132N"),
                "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg),
                "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",             "Battery",         DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((sensor_id & 0x0fff) == ID_RTGN129 && num_valid_v2_bits == 161) {
        if (validate_os_v2_message(decoder, msg, 161, num_valid_v2_bits, 15) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        data = data_make(
                "brand",                 "",                        DATA_STRING, "OS",
                "model",                 "",                        DATA_STRING, _X("Oregon-RTGN129","RTGN129"),
                "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg),
                "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                "battery",             "Battery",         DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((sensor_id & 0x0fff) == ID_RTGN318) {
        if (num_valid_v2_bits == 153 && (validate_os_v2_message(decoder, msg, 153, num_valid_v2_bits, 15) == 0)) {
            float temp_c = get_os_temperature(msg);
            data = data_make(
                    "brand",                 "",                        DATA_STRING, "OS",
                    "model",                 "",                        DATA_STRING, _X("Oregon-RTGN318","RTGN318"),
                    "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg),
                    "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                    "battery",             "Battery",         DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                    "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                    "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                    NULL);
            decoder_output_data(decoder, data);
            return 1;
        }
        else if (num_valid_v2_bits == 201 && (validate_os_v2_message(decoder, msg, 201, num_valid_v2_bits, 21) == 0)) {
            // RF Clock message ??
            return 0;
        }
    }
    else if (sensor_id == ID_THN129 || (sensor_id & 0x0FFF) == ID_RTHN129) {
        if ((validate_os_v2_message(decoder, msg, 137, num_valid_v2_bits, 12) == 0)) {
            float temp_c = get_os_temperature(msg);
            data = data_make(
                    "brand",                 "",                        DATA_STRING, "OS",
                    "model",                 "",                        DATA_STRING, (sensor_id == ID_THN129) ? _X("Oregon-THN129","THN129") : "Oregon-RTHN129",
                    "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg),
                    "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                    "battery",             "Battery",         DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                    "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                    NULL);
            decoder_output_data(decoder, data);
            return 1;
        }
        else if (num_valid_v2_bits == 209 && (validate_os_v2_message(decoder, msg, 209, num_valid_v2_bits, 18) == 0)) {
            // RF Clock message
            return 0;
        }
    }
    else if (sensor_id == ID_BTHGN129) {
        if (validate_os_v2_message(decoder, msg, 185, num_valid_v2_bits, 19) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        // Pressure is given in hPa. You may need to adjust the offset
        // according to your altitude level (600 is a good starting point)
        float pressure = ((msg[7] & 0x0f) | (msg[8] & 0xf0)) * 2 + (msg[8] & 0x01) + 600;
        data = data_make(
                "brand",                 "",                        DATA_STRING, "OS",
                "model",                 "",                        DATA_STRING, _X("Oregon-BTHGN129","BTHGN129"),
                "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg),
                "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                "battery",             "Battery",         DATA_STRING, get_os_battery(msg) ? "LOW" : "OK",
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",             "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, get_os_humidity(msg),
                "pressure_hPa",    "Pressure",        DATA_FORMAT, "%.02f hPa", DATA_DOUBLE, pressure,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_UVR128 && num_valid_v2_bits == 297) {
        if (validate_os_v2_message(decoder, msg, 297, num_valid_v2_bits, 12) != 0)
            return 0;
        int uvidx = get_os_uv(msg);
        data = data_make(
                "model",                    "",                     DATA_STRING, _X("Oregon-UVR128","Oregon Scientific UVR128"),
                "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg),
                "uv",                         "UV Index",     DATA_FORMAT, "%u", DATA_INT, uvidx,
                "battery",                "Battery",        DATA_STRING, get_os_battery(msg)?"LOW":"OK",
                //"channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (num_valid_v2_bits > 16) {
        if (decoder->verbose) {
            fprintf(stderr, "%d bit message received from unrecognized Oregon Scientific v2.1 sensor with device ID %x.\n", num_valid_v2_bits, sensor_id);
            bitrow_printf(msg, 20 * 8, "Message: ");
        }
    }
    else {
        if (decoder->verbose) {
            fprintf(stderr, "\nPossible Oregon Scientific v2.1 message, but sync nibble wasn't found\n");
            bitrow_printf(b, bitbuffer->bits_per_row[0], "Raw Data: ");
        }
    }

    return 0;
}

static int oregon_scientific_v3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];
    data_t *data;

    // Check stream for possible Oregon Scientific v3 protocol data (skip part of first and last bytes to get past sync/startup bit errors)
    if ((((b[0]&0xf) != 0x0f) || (b[1] != 0xff) || ((b[2]&0xc0) != 0xc0))
            && (((b[0]&0xf) != 0x00) || (b[1] != 0x00) || ((b[2]&0xc0) != 0x00))) {
        if (b[3] != 0) {
            if (decoder->verbose)
                bitrow_printf(b, bitbuffer->bits_per_row[0], "Unrecognized Msg in OS v3: ");
        }
        return 0;
    }

    unsigned char msg[BITBUF_COLS] = {0};
    int msg_len = 0;
    unsigned int sync_test_val     = ((unsigned)b[2] << 24) | (b[3] << 16) | (b[4] << 8);
    // Could be extra/dropped bits in stream.    Look for sync byte at expected position +/- some bits in either direction
    for (int pattern_index = 0; pattern_index < 16; pattern_index++) {
        unsigned int mask     = (unsigned int)(0xfff00000 >> pattern_index);
        unsigned int pattern  = (unsigned int)(0xffa00000 >> pattern_index);
        unsigned int pattern2 = (unsigned int)(0xff500000 >> pattern_index);
        unsigned int pattern3 = (unsigned int)(0x00500000 >> pattern_index);
        unsigned int pattern4 = (unsigned int)(0x04600000 >> pattern_index);
        //fprintf(stdout, "OS v3 Sync nibble search - test_val=%08x pattern=%08x    mask=%08x\n", sync_test_val, pattern, mask);
        if (((sync_test_val & mask) != pattern)
                && ((sync_test_val & mask) != pattern2)
                && ((sync_test_val & mask) != pattern3)
                && ((sync_test_val & mask) != pattern4))
            continue;

        // Found sync byte - start working on decoding the stream data.
        // pattern_index indicates    where sync nibble starts, so now we can find the start of the payload
        int start_byte = 3 + (pattern_index >> 3);
        int start_bit  = (pattern_index + 4) & 0x07; // this really looks broken

        int msg_pos = start_byte * 8 + start_bit; // this should be pattern_index + 28; or 20, maybe.
        msg_len = bitbuffer->bits_per_row[0] - msg_pos;
        //fprintf(stderr, "Oregon Scientific v3 Sync test val %08x ok, starting decode at bit %d\n", sync_test_val, msg_pos);

        bitbuffer_extract_bytes(bitbuffer, 0, msg_pos, msg, msg_len);
        reflect_nibbles(msg, (msg_len + 7) / 8);

        break;
    }

    int sensor_id = (msg[0] << 8) | msg[1];
    if (sensor_id == ID_THGR810) {
        if (validate_os_checksum(decoder, msg, 15) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        int humidity = get_os_humidity(msg);
        data = data_make(
                "brand",                    "",                     DATA_STRING, "OS",
                "model",                    "",                     DATA_STRING, _X("Oregon-THGR810","THGR810"),
                "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg),
                "channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",                "Battery",        DATA_STRING, get_os_battery(msg)?"LOW":"OK",
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",             "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                NULL);
        decoder_output_data(decoder, data);
        return 1;                                    //msg[k] = ((msg[k] & 0x0F) << 4) + ((msg[k] & 0xF0) >> 4);
    }
    else if (sensor_id == ID_THN802) {
        if (validate_os_checksum(decoder, msg, 12) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        data = data_make(
                "brand",                    "",                     DATA_STRING, "OS",
                "model",                    "",                     DATA_STRING, _X("Oregon-THN802","THN802"),
                "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg),
                "channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",                "Battery",        DATA_STRING, get_os_battery(msg)?"LOW":"OK",
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_UV800) {
        if (validate_os_checksum(decoder, msg, 13) != 0)
            return 0;
        int uvidx = get_os_uv(msg);
        data = data_make(
                "brand",                    "",                     DATA_STRING, "OS",
                "model",                    "",                     DATA_STRING, _X("Oregon-UV800","UV800"),
                "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg),
                "channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",                "Battery",        DATA_STRING, get_os_battery(msg)?"LOW":"OK",
                "uv",                         "UV Index",     DATA_FORMAT, "%u", DATA_INT, uvidx,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_PCR800) {
        if (validate_os_checksum(decoder, msg, 18) != 0)
            return 0;
        float rain_rate = get_os_rain_rate(msg);
        float total_rain = get_os_total_rain(msg);
        data = data_make(
                "brand",            "",                     DATA_STRING, "OS",
                "model",            "",                     DATA_STRING, _X("Oregon-PCR800","PCR800"),
                "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg),
                "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",        "Battery",        DATA_STRING, get_os_battery(msg)?"LOW":"OK",
                _X("rain_rate_in_h","rain_rate"),    "Rain Rate",    DATA_FORMAT, "%3.1f in/hr", DATA_DOUBLE, rain_rate,
                _X("rain_in","rain_total"), "Total Rain", DATA_FORMAT, "%3.1f in", DATA_DOUBLE, total_rain,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_PCR800a) {
        if (validate_os_checksum(decoder, msg, 18) != 0)
            return 0;
        float rain_rate = get_os_rain_rate(msg);
        float total_rain = get_os_total_rain(msg);
        data = data_make(
                "brand",            "",                     DATA_STRING, "OS",
                "model",            "",                     DATA_STRING, _X("Oregon-PCR800a","PCR800a"),
                "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg),
                "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                "battery",        "Battery",        DATA_STRING, get_os_battery(msg)?"LOW":"OK",
                _X("rain_rate_in_h","rain_rate"),    "Rain Rate",    DATA_FORMAT, "%3.1f in/hr", DATA_DOUBLE, rain_rate,
                _X("rain_in","rain_total"), "Total Rain", DATA_FORMAT, "%3.1f in", DATA_DOUBLE, total_rain,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_WGR800) {
        if (validate_os_checksum(decoder, msg, 17) != 0)
            return 0;
        float gustWindspeed = (msg[5]&0x0f) /10.0F + ((msg[6]>>4)&0x0f) *1.0F + (msg[6]&0x0f) * 10.0F;
        float avgWindspeed = ((msg[7]>>4)&0x0f) / 10.0F + (msg[7]&0x0f) *1.0F + ((msg[8]>>4)&0x0f) * 10.0F;
        float quadrant = (0x0f&(msg[4]>>4))*22.5F;
        data = data_make(
                "brand",            "",                     DATA_STRING,    "OS",
                "model",            "",                     DATA_STRING,    _X("Oregon-WGR800","WGR800"),
                "id",                 "House Code", DATA_INT,         get_os_rollingcode(msg),
                "channel",        "Channel",        DATA_INT,         get_os_channel(msg, sensor_id),
                "battery",        "Battery",        DATA_STRING,    get_os_battery(msg)?"LOW":"OK",
                _X("wind_max_m_s","gust"),             "Gust",             DATA_FORMAT,    "%2.1f m/s",DATA_DOUBLE, gustWindspeed,
                _X("wind_avg_m_s","average"),        "Average",        DATA_FORMAT,    "%2.1f m/s",DATA_DOUBLE, avgWindspeed,
                _X("wind_dir_deg","direction"),    "Direction",    DATA_FORMAT,    "%3.1f degrees",DATA_DOUBLE, quadrant,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((msg[0] == 0x20) || (msg[0] == 0x21) || (msg[0] == 0x22) || (msg[0] == 0x23) || (msg[0] == 0x24)) { // Owl CM160 Readings
        msg[0] = msg[0] & 0x0f;
        if (validate_os_checksum(decoder, msg, 22) != 0)
            return 0;
        float rawAmp = (msg[4] >> 4 << 8 | (msg[3] & 0x0f )<< 4 | msg[3] >> 4);
        unsigned short int ipower = (rawAmp /(0.27*230)*1000);
        data = data_make(
                "brand",    "",                     DATA_STRING, "OS",
                "model",    "",                     DATA_STRING,    _X("Oregon-CM160","CM160"),
                "id",         "House Code", DATA_INT, msg[1]&0x0F,
                "power_W", "Power",         DATA_FORMAT,    "%d W", DATA_INT, ipower,
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (msg[0] == 0x26) { // Owl CM180 readings
        msg[0]    = msg[0] & 0x0f;
        int valid = validate_os_checksum(decoder, msg, 23);
        for (int k = 0; k < BITBUF_COLS; k++) { // Reverse nibbles
            msg[k] = (msg[k] & 0xF0) >> 4 | (msg[k] & 0x0F) << 4;
        }
        unsigned short int ipower = power(msg);
        unsigned long long itotal = total(msg);
        float total_energy        = itotal / 3600 / 1000.0;
        if (itotal && valid == 0) {
            data = data_make(
                    "brand",            "",                     DATA_STRING, "OS",
                    "model",            "",                     DATA_STRING,    _X("Oregon-CM180","CM180"),
                    "id",                 "House Code", DATA_INT, msg[1]&0x0F,
                    "power_W",        "Power",            DATA_FORMAT,    "%d W",DATA_INT, ipower,
                    "energy_kWh", "Energy",         DATA_FORMAT,    "%2.1f kWh",DATA_DOUBLE, total_energy,
                    NULL);
            decoder_output_data(decoder, data);
            return 1;
        }
        else if (!itotal) {
            data = data_make(
                    "brand",    "",                     DATA_STRING, "OS",
                    "model",    "",                     DATA_STRING,    _X("Oregon-CM180","CM180"),
                    "id",         "House Code", DATA_INT, msg[1]&0x0F,
                    "power_W", "Power",         DATA_FORMAT,    "%d W",DATA_INT, ipower,
                    NULL);
            decoder_output_data(decoder, data);
            return 1;
        }
    }
    else if ((msg[0] != 0) && (msg[1]!= 0)) { // sync nibble was found    and some data is present...
        if (decoder->verbose) {
            fprintf(stderr, "Message received from unrecognized Oregon Scientific v3 sensor.\n");
            bitrow_printf(msg, msg_len, "Message: ");
            bitrow_printf(b, bitbuffer->bits_per_row[0], "Raw: ");
        }
    }
    else if (b[3] != 0 ) {
        if (decoder->verbose) {
            fprintf(stderr, "Possible Oregon Scientific v3 message, but sync nibble wasn't found\n");
            bitrow_printf(b, bitbuffer->bits_per_row[0], "Raw Data: ");
        }
    }
    return 0;
}

static int oregon_scientific_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = oregon_scientific_v2_1_decode(decoder, bitbuffer);
    if (ret == 0)
        ret = oregon_scientific_v3_decode(decoder, bitbuffer);
    return ret;
}

static char *output_fields[] = {
        "brand",
        "model",
        "id",
        "channel",
        "battery",
        "temperature_C",
        "humidity",
        "rain_rate", // TODO: remove this
        "rain_rate_mm_h",
        "rain_rate_in_h",
        "rain_total", // TODO: remove this
        "rain_mm",
        "rain_in",
        "gust",      // TODO: remove this
        "average",   // TODO: remove this
        "direction", // TODO: remove this
        "wind_max_m_s",
        "wind_avg_m_s",
        "wind_dir_deg",
        "pressure_hPa",
        "uv",
        "power_W",
        "energy_kWh",
        NULL,
};

r_device oregon_scientific = {
        .name        = "Oregon Scientific Weather Sensor",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 440, // Nominal 1024Hz (488µs), but pulses are shorter than pauses
        .long_width  = 0,   // not used
        .reset_limit = 2400,
        .decode_fn   = &oregon_scientific_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
