// Seek Thermal Viewer/Streamer
// http://github.com/fnoop/maverick

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

enum CustomLineTypes {
    LINE_AA = 16
};

auto isConnectedToServer = false;
auto isWindowMode = true;
auto fireWarningText = "WARNING";
auto fireThresholdCelcius = 45;
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
    return (fahrenheit - 32.0) * 5.0 / 9.0;
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

void draw_text(Mat &outframe, const char* text, const Point &coord, Scalar color)
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

    if (maxtemp > fireThresholdCelcius) {
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

void connectToServer(sf::TcpSocket &socket, const char *remoteAddress, const int remotePort)
{
    socket.disconnect();

    printf("Attempting to connect to %s:%d\n", remoteAddress, remotePort);

    while (true)
    {
        if (socket.connect(remoteAddress, remotePort) == sf::Socket::Done)
        {
            break;
        }
    }

    printf("Successfully connected.");

    isConnectedToServer = true;
}

int main(int argc, char **argv)
{
    args::ArgumentParser parser("Seek Thermal Data Streamer");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> arg_target_host(parser, "arg_target_host", "Target host", {"host"});
    args::ValueFlag<std::string> arg_target_port(parser, "arg_target_port", "Target port", {"port"});

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

    // Variables for socket mode
    sf::TcpSocket socket;
    const char *remoteAddress = nullptr;
    int remotePort = -1;

    // Variables for window mode
    cv::String windowName = "Display Window";

    if (arg_target_host && arg_target_port)
    {
        isWindowMode = false;
        remoteAddress = args::get(arg_target_host).c_str();
        remotePort = std::stoi(args::get(arg_target_port));
    }
    else
    {
        std::cout << "Operating in window mode." << std::endl;

        namedWindow(windowName, 0x00000000 /* WINWOD_NORMAL */);
        setWindowProperty(windowName, 2 /* WND_PROP_ASPECT_RATIO */, 0x00000000 /* WINDOW_KEEPRATIO */);
        resizeWindow(windowName, seekFrame.cols, seekFrame.rows);
    }

    // Main loop to retrieve frames from camera and output
    while (!sigflag)
    {
        if (!isConnectedToServer && !isWindowMode)
        {
            connectToServer(socket, remoteAddress, remotePort);
        }

        // If signal for interrupt/termination was received, break out of main loop and exit
        if (!seek->read(seekFrame))
        {
            return -1;
        }

        // Retrieve frame from seek and process
        process_frame(seekFrame, outFrame, 3.0f, 11, 0, seek->device_temp_sensor());

        cv::putText(
            outFrame,
            getTime().str(),
            Point(10, 30),
            FONT_HERSHEY_COMPLEX,
            1,
            Scalar(0, 0, 0),
            1.5, /* Thickness */
            CustomLineTypes::LINE_AA);

        if (!isWindowMode)
        {
            std::vector<uchar> imageBuffer;
            imencode("image.jpeg", outFrame, imageBuffer);

            unsigned long imageSize = imageBuffer.size() * sizeof(uchar);
            char imageSizeText[100];
            sprintf(imageSizeText, ":::%0.10lu", imageSize);

            if (isConnectedToServer && socket.send(imageSizeText, strlen(imageSizeText)) != sf::Socket::Done)
            {
                isConnectedToServer = false;
            }

            if (isConnectedToServer && socket.send(&imageBuffer[0], imageSize) != sf::Socket::Done)
            {
                isConnectedToServer = false;
            }
        }
        else
        {
            auto key = cv::waitKey(10);

            if (key == 's') {
                const auto now = std::chrono::system_clock::now();
                auto imageCode = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                char fileName[100];
                sprintf(fileName, "%lu.jpeg", imageCode);
                printf("Attempting to save image as %s.\n", fileName);
                cv::imwrite(fileName, outFrame);
            }

            cv::imshow(windowName, outFrame);
        }
    }

    std::cout << "Break signal detected, exiting" << std::endl;
    return 0;
}
