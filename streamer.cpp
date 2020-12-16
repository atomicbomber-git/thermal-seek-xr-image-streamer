#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "seek.h"
#include "SeekCam.h"
#include <iostream>
#include <string>
#include <csignal>
#include <SFML/Network.hpp>
#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>
#include <utility>
#include <chrono>
#include "args.h"

using namespace cv;
using namespace LibSeek;

// Setup sig handling
static volatile sig_atomic_t sigflag = 0;

enum CustomLineTypes
{
    LINE_AA = 16
};

enum OperationMode
{
    ConnectToServer,
    WaitForCommand,
    SendImage,
    Exit,
};

const auto DEFAULT_HOST = "127.0.0.1";
const auto DEFAULT_PORT = 9000;

double preAdd = -32.0;
double multiplier = 5.0 / 9.0;
double postAdd = 0;

auto fireWarningText = "DEMAM";
auto fireThresholdCelcius = 35;
void connectToServer(sf::TcpSocket &socket, const char *remoteAddress, const int remotePort);

std::stringstream getTime();

void handle_sig(int sig)
{
    (void)sig;
    sigflag = 1;
}

double device_sensor_to_k(double sensor)
{
    // formula from http://aterlux.ru/article/ntcresistor-en
    double ref_temp = 297.0;    // 23C from table
    double ref_sensor = 6616.0; // ref value from table
    double beta = 200;          // best beta coef we've found
    double part3 = log(sensor) - log(ref_sensor);
    double parte = part3 / beta + 1.0 / ref_temp;
    return 1.0 / parte;
}

double temp_from_raw(int x, double device_k)
{
    // Constants below are taken from linear trend line in Excel.
    // -273 is translation of Kelvin to Celsius
    // 330 is max temperature supported by Seek device
    // 16384 is full 14 bits value, max possible ()
    double base = x * 330 / 16384.0;
    double lin_k = -1.5276;        // derived from Excel linear model
    double lin_offset = -470.8979; // same Excel model

    auto fahrenheit = base - device_k * lin_k + lin_offset - 273.0;
    return ((fahrenheit + preAdd) * multiplier) + postAdd;
}

void overlay_values(Mat &outframe, Point coord, const Scalar &color)
{
    int gap = 2;
    int arrLen = 7;
    int weight = 1;
    line(outframe, coord - Point(-arrLen, -arrLen), coord - Point(-gap, -gap), color, weight);
    line(outframe, coord - Point(arrLen, arrLen), coord - Point(gap, gap), color, weight);
    line(outframe, coord - Point(-arrLen, arrLen), coord - Point(-gap, gap), color, weight);
    line(outframe, coord - Point(arrLen, -arrLen), coord - Point(gap, -gap), color, weight);
}

void draw_temp(Mat &outframe, double temp, const Point &coord, Scalar color)
{
    char txt[64];
    sprintf(txt, "%5.1f", temp);
    putText(outframe, txt, coord - Point(40, -20), FONT_HERSHEY_COMPLEX, 1, std::move(color), 2, CustomLineTypes::LINE_AA);
}

void draw_text(Mat &outframe, const char *text, const Point &coord, Scalar color)
{
    putText(outframe, text, coord - Point(40, -20), FONT_HERSHEY_COMPLEX, 1, std::move(color), 2, CustomLineTypes::LINE_AA);
}

// Function to process a raw (corrected) seek frame
void process_frame(Mat &inframe, Mat &outframe, float scale, int colormap, int rotate, int device_temp_sensor)
{
    Mat frame_g8_nograd, frame_g16; // Transient Mat containers for processing

    // get raw max/min/central values
    double min, max, central;
    minMaxIdx(inframe, &min, &max);
    Scalar valat = inframe.at<uint16_t>(Point(inframe.cols / 2.0, inframe.rows / 2.0));
    central = valat[0];

    double device_k = device_sensor_to_k(device_temp_sensor);

    double mintemp = temp_from_raw(min, device_k);
    double maxtemp = temp_from_raw(max, device_k);
    double centraltemp = temp_from_raw(central, device_k);

    // printf("rmin,rmax,central,devtempsns: %d %d %d %d\t", (int)min, (int)max, (int)central, (int)device_temp_sensor);
    // printf("min-max-center-device: %.1f %.1f %.1f %.1f\n", mintemp, maxtemp, centraltemp, device_k - 273.0);

    normalize(inframe, frame_g16, 0, 65535, NORM_MINMAX);

    // Convert seek CV_16UC1 to CV_8UC1
    frame_g16.convertTo(frame_g8_nograd, CV_8UC1, 1.0 / 256.0);

    // Rotate image
    if (rotate == 90)
    {
        transpose(frame_g8_nograd, frame_g8_nograd);
        flip(frame_g8_nograd, frame_g8_nograd, 1);
    }
    else if (rotate == 180)
    {
        flip(frame_g8_nograd, frame_g8_nograd, -1);
    }
    else if (rotate == 270)
    {
        transpose(frame_g8_nograd, frame_g8_nograd);
        flip(frame_g8_nograd, frame_g8_nograd, 0);
    }

    Point minp, maxp, centralp;
    minMaxLoc(frame_g8_nograd, NULL, NULL, &minp, &maxp); // doing it here, so we take rotation into account
    centralp = Point(frame_g8_nograd.cols / 2.0, frame_g8_nograd.rows / 2.0);
    minp *= scale;
    maxp *= scale;
    centralp *= scale;

    // Resize image: http://docs.opencv.org/3.2.0/da/d54/group__imgproc__transform.html#ga5bb5a1fea74ea38e1a5445ca803ff121
    // Note this is expensive computationally, only do if option set != 1
    if (scale != 1.0)
        resize(frame_g8_nograd, frame_g8_nograd, Size(), scale, scale, INTER_LINEAR);

    // add gradient
    Mat frame_g8(Size(frame_g8_nograd.cols + 20, frame_g8_nograd.rows), CV_8U, Scalar(128));
    for (int r = 0; r < frame_g8.rows - 1; r++)
    {
        frame_g8.row(r).setTo(255.0 * (frame_g8.rows - r) / ((float)frame_g8.rows));
    }
    frame_g8_nograd.copyTo(frame_g8(Rect(0, 0, frame_g8_nograd.cols, frame_g8_nograd.rows)));

    // Apply colormap: http://docs.opencv.org/3.2.0/d3/d50/group__imgproc__colormap.html#ga9a805d8262bcbe273f16be9ea2055a65
    if (colormap != -1)
    {
        applyColorMap(frame_g8, outframe, colormap);
    }
    else
    {
        cv::cvtColor(frame_g8, outframe, cv::COLOR_GRAY2BGR);
    }

    // overlay marks
    draw_temp(outframe, mintemp, Point(outframe.cols - 49, outframe.rows - 29), Scalar(255, 255, 255));
    draw_temp(outframe, mintemp, Point(outframe.cols - 51, outframe.rows - 31), Scalar(0, 0, 0));
    draw_temp(outframe, mintemp, Point(outframe.cols - 50, outframe.rows - 30), Scalar(255, 0, 0));

    draw_temp(outframe, maxtemp, Point(outframe.cols - 49, 0), Scalar(255, 255, 255));
    draw_temp(outframe, maxtemp, Point(outframe.cols - 51, 2), Scalar(0, 0, 0));
    draw_temp(outframe, maxtemp, Point(outframe.cols - 50, 1), Scalar(0, 0, 255));

    draw_temp(outframe, centraltemp, centralp + Point(-1, -1), Scalar(255, 255, 255));
    draw_temp(outframe, centraltemp, centralp + Point(1, 1), Scalar(0, 0, 0));
    draw_temp(outframe, centraltemp, centralp + Point(0, 0), Scalar(128, 128, 128));

    overlay_values(outframe, centralp + Point(-1, -1), Scalar(0, 0, 0));
    overlay_values(outframe, centralp + Point(1, 1), Scalar(255, 255, 255));
    overlay_values(outframe, centralp, Scalar(128, 128, 128));

    overlay_values(outframe, minp + Point(-1, -1), Scalar(0, 0, 0));
    overlay_values(outframe, minp + Point(1, 1), Scalar(255, 255, 255));
    overlay_values(outframe, minp, Scalar(255, 0, 0));

    overlay_values(outframe, maxp + Point(-1, -1), Scalar(0, 0, 0));
    overlay_values(outframe, maxp + Point(1, 1), Scalar(255, 255, 255));
    overlay_values(outframe, maxp, Scalar(0, 0, 255));

    if (maxtemp > fireThresholdCelcius)
    {
        draw_text(outframe, fireWarningText, maxp + Point(-1, -1), Scalar(255, 255, 255));
        draw_text(outframe, fireWarningText, maxp + Point(1, 1), Scalar(0, 0, 0));
        draw_text(outframe, fireWarningText, maxp + Point(0, 0), Scalar(0, 0, 255));
    }
}

std::stringstream getTime()
{
    auto time = std::time(nullptr);

    std::stringstream stringStream;

    stringStream << localtime(&time)->tm_year + 1900
                 << " " << localtime(&time)->tm_mon + 1
                 << "/" << localtime(&time)->tm_mday
                 << " " << localtime(&time)->tm_hour
                 << ":" << localtime(&time)->tm_min
                 << ":" << localtime(&time)->tm_sec;

    return stringStream;
}

void connect(sf::TcpSocket &socket, const char *remoteAddress, const int remotePort)
{
    printf("Attempting to connect...\n");

    while (true)
    {
        if (socket.connect(remoteAddress, remotePort) == sf::Socket::Done)
        {
            printf("Successfully connected...\n");
            break;
        }
    }
}

void printSocketStatus(sf::Socket::Status &socketStatus)
{
    switch (socketStatus)
    {
    case sf::Socket::Done:
        printf("DONE.\n");
        break;
    case sf::Socket::NotReady:
        printf("NOT_READY.\n");
        break;
    case sf::Socket::Partial:
        printf("PARTIAL.\n");
        break;
    case sf::Socket::Disconnected:
        printf("DISCONNECTED.\n");
        break;
    case sf::Socket::Error:
        printf("ERROR.\n");
        break;
    default:
        printf("UNKNOWN.\n");
        break;
    }
}

bool sendImage(sf::TcpSocket &socket, std::vector<uchar> &buffer, cv::Mat &source)
{
    cv::imencode("image.jpeg", source, buffer);
    unsigned long imageSize = buffer.size() * sizeof(uchar);

    char imageSizeText[100];
    sprintf(imageSizeText, ":::%0.10lu", imageSize);

    sf::Socket::Status socketStatus;

    socketStatus = socket.send(imageSizeText, strlen(imageSizeText));
    if ((socketStatus == sf::Socket::Disconnected) || (socketStatus == sf::Socket::Error))
    {
        return false;
    }

    socketStatus = socket.send(&buffer[0], imageSize);
    if ((socketStatus == sf::Socket::Disconnected) || (socketStatus == sf::Socket::Error))
    {
        return false;
    }

    return true;
}

void printConnectingToServerInfo() {
    std::cout << "Diconnected from the server." << std::endl;
    std::cout << "Attempting to connect to the server..." << std::endl;
}

void writeLogMessage(char const *logMessage) {
    // std::cout << logMessage << std::endl;
}

int main(int argc, char const *argv[])
{
    args::ArgumentParser parser("Seek Thermal Data Streamer");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> arg_target_host(parser, "arg_target_host", "Target host", {"host"});
    args::ValueFlag<std::string> arg_target_port(parser, "arg_target_port", "Target port", {"port"});
    args::ValueFlag<std::string> arg_preadd(parser, "arg_preadd", "Pre-Addition Temp Shift", {"preadd"});
    args::ValueFlag<std::string> arg_postadd(parser, "arg_postadd", "Post-Addition Temp Shift", {"postadd"});
    args::ValueFlag<std::string> arg_multiplier(parser, "arg_multiplier", "Multiplier for Temp", {"multiplier"});

    // Parse command line arguments
    try
    {
        parser.ParseCLI(argc, argv);
    }
    catch (args::Help)
    {
        std::cout << parser;
        return 0;
    }
    catch (args::ParseError e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    catch (args::ValidationError e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    if (arg_preadd) {
        preAdd = std::stod(args::get(arg_preadd));
    }

    if (arg_postadd) {
        postAdd = std::stod(args::get(arg_postadd));
    }

    if (arg_multiplier) {
        multiplier = std::stod(args::get(arg_multiplier));
    }

    // Register signals
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    // Setup seek camera
    LibSeek::SeekCam *seek;
    LibSeek::SeekThermal seekClassic("");

    seek = &seekClassic;

    if (!seek->open())
    {
        std::cout << "Error accessing camera" << std::endl;
        return -1;
    }

    // Mat containers for seek frames
    Mat seekFrame, outFrame;

    // Retrieve a single frame, resize to requested scaling value and then determine size of matrix
    //  so we can size the VideoWriter stream correctly
    if (!seek->read(seekFrame))
    {
        std::cout << "Failed to read initial frame from camera, exiting" << std::endl;
        return 1;
    }

    /* Will be retrieved from cli arguments */
    const char *remoteAddress = DEFAULT_HOST;
    int remotePort = DEFAULT_PORT;
    if (arg_target_host && arg_target_port)
    {
        remoteAddress = args::get(arg_target_host).c_str();
        remotePort = std::stoi(args::get(arg_target_port));
    }

    bool isConnected = false;
    sf::TcpSocket socket;

    std::vector<uchar> imageBuffer;
    sf::Socket::Status socketStatus;

    char receivedData[100];
    std::size_t receivedCount;
    std::size_t receivedCountSum = 0;

    auto mode = OperationMode::ConnectToServer;
    auto num = 1;

    while (!sigflag)
    {
        switch (mode)
        {
        case OperationMode::ConnectToServer:
            writeLogMessage("Connecting to server...");

            if (socket.connect(remoteAddress, remotePort) == sf::Socket::Done) {
                writeLogMessage("Successfully connected.");
                mode = OperationMode::WaitForCommand;
            }
            
            break;
        case OperationMode::WaitForCommand:
            writeLogMessage("Waiting for command.");

            if (socket.receive(receivedData, 1, receivedCount) != sf::Socket::Done) {
                mode = OperationMode::ConnectToServer;
                printConnectingToServerInfo();
                break;
            }

            receivedCountSum += receivedCount;
            
            if (receivedCount >= 1) {
                mode = OperationMode::SendImage;
                receivedCountSum = 0;
                break;
            }

            break;
        case OperationMode::SendImage:
            writeLogMessage("Sending image...");

            /* If signal for interrupt/termination was received, break out of main loop and exit */
            if (!seek->read(seekFrame))
            {
                mode = OperationMode::Exit;
                break;
            }

            process_frame(seekFrame, outFrame, 4.0f, 11, 90, seek->device_temp_sensor());

            if (!sendImage(socket, imageBuffer, outFrame)) {
                mode = OperationMode::ConnectToServer;
                printConnectingToServerInfo();
                break;
            }
            
            mode = OperationMode::WaitForCommand;
            break;
        case OperationMode::Exit:
            writeLogMessage("Exiting...");
            goto exit_loop;
        default:
            break;
        }
    }

    exit_loop: ;

    return 0;
}