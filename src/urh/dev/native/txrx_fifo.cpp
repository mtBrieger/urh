/*
 use compiler flags -std=c++11 -lboost_system -lboost_date_time -lboost_program_options -lboost_filesystem -lboost_thread-mt -luhd -g -o txrx_files
 use 'sudo rm /tmp/tx_fifo; rm /tmp/rx_fifo' to remove fifos and stop process
 */

#include "wavetable.hpp"
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/static.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/math/special_functions/round.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <csignal>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <chrono>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


namespace po = boost::program_options;


int tx_fd, rx_fd;
/***********************************************************************
 * Signal handlers
 **********************************************************************/
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

/***********************************************************************
 * Utilities
 **********************************************************************/
inline bool file_exists(const std::string& name) {
    return ( access( name.c_str(), F_OK ) != -1 );
}

/***********************************************************************
 * send_from_file function
 **********************************************************************/
void send_from_file(uhd::tx_streamer::sptr tx_stream,
                    const std::string& file,
                    size_t samps_per_buff)
{
    //std::cout.precision(16);
    uhd::tx_metadata_t md;
    md.start_of_burst = false;
    md.end_of_burst   = false;
    std::vector<std::complex<float>> buff(2040);
    std::cout << "make" << std::endl;
    if (mkfifo(file.c_str(), 0666) == -1) {
        std::cout << "mkfifo error: " << std::strerror(errno) << std::endl;
    }

    do {
        tx_fd = open(file.c_str(), O_RDONLY|O_NONBLOCK);
        std::cout << "opend" << std::endl;
    } while (tx_fd < 0 and not stop_signal_called and file_exists(file));

    long sample_count = 0;

    while (not md.end_of_burst and not stop_signal_called and file_exists(file)) {


        int len = read(tx_fd, &buff.front(), 2040 * sizeof(std::complex<float>));

        if (len < 1) {
            continue;
        }

        size_t num_tx_samps = size_t(len / sizeof(std::complex<float>));

        sample_count += num_tx_samps;

        tx_stream->send(&buff.front(), num_tx_samps, md, 0.1);
    }

    close(tx_fd);
    std::remove(file.c_str());
}


/***********************************************************************
 * recv_to_file function
 **********************************************************************/
void recv_to_file(uhd::usrp::multi_usrp::sptr usrp,
                  const std::string& file,
                  size_t samps_per_buff)
{

    // create a receive streamer
    uhd::stream_args_t stream_args("fc32", "sc16");
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    // Prepare buffer and metadata
    uhd::rx_metadata_t md;
    std::vector<std::complex<float>> buff(1020);
    std::remove(file.c_str());

    bool overflow_message = true;

    if (mkfifo(file.c_str(), 0666) == -1) {
        std::cout << "mkfifo error: " << std::strerror(errno) << std::endl;
        //exit(0);
    }

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.stream_now = true;
    stream_cmd.time_spec  = uhd::time_spec_t();
    rx_stream->issue_stream_cmd(stream_cmd);

    do {
        rx_fd = open(file.c_str(), O_WRONLY|O_NONBLOCK); //O_WRONLY|O_NONBLOCK)  O_RDWR
    } while (rx_fd < 0 and not stop_signal_called and file_exists(file));

    while (not stop_signal_called and file_exists(file)) {
        //auto start = std::chrono::steady_clock::now();

        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md);


        do {
            rx_fd = open(file.c_str(), O_WRONLY|O_NONBLOCK); //O_WRONLY|O_NONBLOCK)  O_RDWR
        } while (rx_fd < 0 and not stop_signal_called and file_exists(file));


        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            break;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            if (overflow_message) {
                overflow_message = false;
                std::cerr << boost::format(
                                           "Got an overflow indication. Please consider the following:\n"
                                           "  Your write medium must sustain a rate of %fMB/s.\n"
                                           "  Dropped samples will not be written to the file.\n"
                                           "  Please modify this example for your purposes.\n"
                                           "  This message will not appear again.\n")
                % (usrp->get_rx_rate() * sizeof(std::complex<float>) / 1e6);
            }
            continue;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            throw std::runtime_error(str(boost::format("Receiver error %s") % md.strerror()));
        }


        write(rx_fd, &buff.front(), num_rx_samps * sizeof(std::complex<float>)); //num_rx_samps
        close(rx_fd);

        //auto end = std::chrono::steady_clock::now();
        //std::cout << "Elapsed time in milliseconds : " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" << std::endl;
    }

    std::cout << "close" << std::endl;
    // Shut down receiver
    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);

    close(rx_fd);
    std::remove(file.c_str());
    std::cout << "closed" << std::endl;
}


/***********************************************************************
 * Main function
 **********************************************************************/
int UHD_SAFE_MAIN(int argc, char* argv[])
{
    uhd::set_thread_priority_safe();

    // transmit variables to be set by po
    std::string tx_args, tx_file, tx_ant, tx_subdev, ref, tx_channels;
    double tx_rate, tx_freq, tx_gain, tx_bw;

    // receive variables to be set by po
    std::string rx_args, rx_file, type, rx_ant, rx_subdev, rx_channels;
    size_t spb;
    double rx_rate, rx_freq, rx_gain, rx_bw;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
    ("help", "help message")
    ("tx-args", po::value<std::string>(&tx_args)->default_value(""), "uhd transmit device address args")
    ("rx-args", po::value<std::string>(&rx_args)->default_value(""), "uhd receive device address args")
    ("tx-file", po::value<std::string>(&tx_file)->default_value("/tmp/tx_fifo"), "name of the fifo to read binary samples from")
    ("rx-file", po::value<std::string>(&rx_file)->default_value("/tmp/rx_fifo"), "name of the fifo to write binary samples to")
    ("type", po::value<std::string>(&type)->default_value("float"), "sample type in file: double, float, or short")
    ("spb", po::value<size_t>(&spb)->default_value(10000), "samples per buffer, 0 for default")
    ("tx-rate", po::value<double>(&tx_rate)->default_value(double(1e6)), "rate of transmit outgoing samples")
    ("rx-rate", po::value<double>(&rx_rate)->default_value(double(1e6)), "rate of receive incoming samples")
    ("tx-freq", po::value<double>(&tx_freq)->default_value(double(868e6)), "transmit RF center frequency in Hz")
    ("rx-freq", po::value<double>(&rx_freq)->default_value(double(868e6)), "receive RF center frequency in Hz")
    ("tx-gain", po::value<double>(&tx_gain)->default_value(double(0.5)), "normalized gain for the transmit RF chain")
    ("rx-gain", po::value<double>(&rx_gain)->default_value(double(0.5)), "normalized gain for the receive RF chain")
    ("tx-ant", po::value<std::string>(&tx_ant), "transmit antenna selection")
    ("rx-ant", po::value<std::string>(&rx_ant), "receive antenna selection")
    ("tx-subdev", po::value<std::string>(&tx_subdev), "transmit subdevice specification")
    ("rx-subdev", po::value<std::string>(&rx_subdev), "receive subdevice specification")
    ("tx-bw", po::value<double>(&tx_bw)->default_value(double(1e6)), "analog transmit filter bandwidth in Hz")
    ("rx-bw", po::value<double>(&rx_bw)->default_value(double(1e6)), "analog receive filter bandwidth in Hz")
    ("ref", po::value<std::string>(&ref)->default_value("internal"), "clock reference (internal, external, mimo)")
    ("tx-channels", po::value<std::string>(&tx_channels)->default_value("0"), "which TX channel(s) to use (specify \"0\", \"1\", \"0,1\", etc)")
    ("rx-channels", po::value<std::string>(&rx_channels)->default_value("0"), "which RX channel(s) to use (specify \"0\", \"1\", \"0,1\", etc)")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help")) {
        std::cout << boost::format("TXRX to File %s") % desc << std::endl;
        return ~0;
    }

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the transmit usrp device with: %s...") % tx_args
    << std::endl;
    uhd::usrp::multi_usrp::sptr tx_usrp = uhd::usrp::multi_usrp::make(tx_args);
    std::cout << std::endl;
    std::cout << boost::format("Creating the receive usrp device with: %s...") % rx_args
    << std::endl;
    uhd::usrp::multi_usrp::sptr rx_usrp = uhd::usrp::multi_usrp::make(rx_args);

    // always select the subdevice first, the channel mapping affects the other settings
    if (vm.count("tx-subdev"))
        tx_usrp->set_tx_subdev_spec(tx_subdev);
    if (vm.count("rx-subdev"))
        rx_usrp->set_rx_subdev_spec(rx_subdev);

    // detect which channels to use
    std::vector<std::string> tx_channel_strings;
    std::vector<size_t> tx_channel_nums;
    boost::split(tx_channel_strings, tx_channels, boost::is_any_of("\"',"));
    for (size_t ch = 0; ch < tx_channel_strings.size(); ch++) {
        size_t chan = std::stoi(tx_channel_strings[ch]);
        if (chan >= tx_usrp->get_tx_num_channels()) {
            throw std::runtime_error("Invalid TX channel(s) specified.");
        } else
            tx_channel_nums.push_back(std::stoi(tx_channel_strings[ch]));
    }
    std::vector<std::string> rx_channel_strings;
    std::vector<size_t> rx_channel_nums;
    boost::split(rx_channel_strings, rx_channels, boost::is_any_of("\"',"));
    for (size_t ch = 0; ch < rx_channel_strings.size(); ch++) {
        size_t chan = std::stoi(rx_channel_strings[ch]);
        if (chan >= rx_usrp->get_rx_num_channels()) {
            throw std::runtime_error("Invalid RX channel(s) specified.");
        } else
            rx_channel_nums.push_back(std::stoi(rx_channel_strings[ch]));
    }

    // Lock mboard clocks
    tx_usrp->set_clock_source(ref);
    rx_usrp->set_clock_source(ref);

    std::cout << boost::format("Using TX Device: %s") % tx_usrp->get_pp_string()
    << std::endl;
    std::cout << boost::format("Using RX Device: %s") % rx_usrp->get_pp_string()
    << std::endl;

    std::cout << boost::format("Setting TX Rate: %f Msps...") % (tx_rate / 1e6)
    << std::endl;
    tx_usrp->set_tx_rate(tx_rate);
    std::cout << boost::format("Actual TX Rate: %f Msps...")
    % (tx_usrp->get_tx_rate() / 1e6)
    << std::endl
    << std::endl;

    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rx_rate / 1e6)
    << std::endl;
    rx_usrp->set_rx_rate(rx_rate);
    std::cout << boost::format("Actual RX Rate: %f Msps...")
    % (rx_usrp->get_rx_rate() / 1e6)
    << std::endl
    << std::endl;


    for (size_t ch = 0; ch < tx_channel_nums.size(); ch++) {
        size_t channel = tx_channel_nums[ch];
        if (tx_channel_nums.size() > 1) {
            std::cout << "Configuring TX Channel " << channel << std::endl;
        }
        std::cout << boost::format("Setting TX Freq: %f MHz...") % (tx_freq / 1e6)
        << std::endl;
        uhd::tune_request_t tx_tune_request(tx_freq);
        tx_usrp->set_tx_freq(tx_tune_request, channel);
        std::cout << boost::format("Actual TX Freq: %f MHz...")
        % (tx_usrp->get_tx_freq(channel) / 1e6)
        << std::endl
        << std::endl;

        // set the rf gain
        if (vm.count("tx-gain")) {
            std::cout << boost::format("Setting normalized TX Gain: %f...") % tx_gain
            << std::endl;
            tx_usrp->set_normalized_tx_gain(tx_gain, channel);
            std::cout << boost::format("Actual TX Gain: %f dB...")
            % tx_usrp->get_tx_gain(channel)
            << std::endl
            << std::endl;
        }

        // set the analog frontend filter bandwidth
        if (vm.count("tx-bw")) {
            std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % (tx_bw / 1e6)
            << std::endl;
            tx_usrp->set_tx_bandwidth(tx_bw, channel);
            std::cout << boost::format("Actual TX Bandwidth: %f MHz...")
            % tx_usrp->get_tx_bandwidth(channel)
            << std::endl
            << std::endl;
        }

        // set the antenna
        if (vm.count("tx-ant"))
            tx_usrp->set_tx_antenna(tx_ant, channel);
    }

    for (size_t ch = 0; ch < rx_channel_nums.size(); ch++) {
        size_t channel = rx_channel_nums[ch];
        if (rx_channel_nums.size() > 1) {
            std::cout << "Configuring RX Channel " << channel << std::endl;
        }

        std::cout << boost::format("Setting RX Freq: %f MHz...") % (rx_freq / 1e6)
        << std::endl;
        uhd::tune_request_t rx_tune_request(rx_freq);
        rx_usrp->set_rx_freq(rx_tune_request, channel);
        std::cout << boost::format("Actual RX Freq: %f MHz...")
        % (rx_usrp->get_rx_freq(channel) / 1e6)
        << std::endl
        << std::endl;

        // set the receive rf gain
        if (vm.count("rx-gain")) {
            std::cout << boost::format("Setting normalized RX Gain: %f ...") % rx_gain
            << std::endl;
            rx_usrp->set_normalized_rx_gain(rx_gain, channel);
            std::cout << boost::format("Actual RX Gain: %f dB...")
            % rx_usrp->get_rx_gain(channel)
            << std::endl
            << std::endl;
        }

        // set the receive analog frontend filter bandwidth
        if (vm.count("rx-bw")) {
            std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (rx_bw / 1e6)
            << std::endl;
            rx_usrp->set_rx_bandwidth(rx_bw, channel);
            std::cout << boost::format("Actual RX Bandwidth: %f MHz...")
            % (rx_usrp->get_rx_bandwidth(channel) / 1e6)
            << std::endl
            << std::endl;
        }

        // set the receive antenna
        if (vm.count("rx-ant"))
            rx_usrp->set_rx_antenna(rx_ant, channel);
    }


    // create a transmit streamer
    uhd::stream_args_t stream_args("fc32", "sc16");
    uhd::tx_streamer::sptr tx_stream = tx_usrp->get_tx_stream(stream_args);


    // Check Ref and LO Lock detect
    std::vector<std::string> tx_sensor_names, rx_sensor_names;
    tx_sensor_names = tx_usrp->get_tx_sensor_names(0);
    if (std::find(tx_sensor_names.begin(), tx_sensor_names.end(), "lo_locked")
        != tx_sensor_names.end()) {
        uhd::sensor_value_t lo_locked = tx_usrp->get_tx_sensor("lo_locked", 0);
        std::cout << boost::format("Checking TX: %s ...") % lo_locked.to_pp_string()
        << std::endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
    }
    rx_sensor_names = rx_usrp->get_rx_sensor_names(0);
    if (std::find(rx_sensor_names.begin(), rx_sensor_names.end(), "lo_locked")
        != rx_sensor_names.end()) {
        uhd::sensor_value_t lo_locked = rx_usrp->get_rx_sensor("lo_locked", 0);
        std::cout << boost::format("Checking RX: %s ...") % lo_locked.to_pp_string()
        << std::endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
    }


    std::signal(SIGINT, &sig_int_handler);

    // reset usrp time to prepare for transmit/receive
    std::cout << boost::format("Setting device timestamp to 0...") << std::endl;
    tx_usrp->set_time_now(uhd::time_spec_t(0.0));

    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // start transmit thread
    boost::thread_group transmit_thread;
    transmit_thread.create_thread(boost::bind(&send_from_file, tx_stream, tx_file, spb));

    // recv to file
    recv_to_file(rx_usrp, rx_file, spb);

    // clean up transmit thread
    stop_signal_called = true;
    transmit_thread.join_all();


    // finished
    std::cout << std::endl << "done" << std::endl << std::endl;
    return EXIT_SUCCESS;
}
