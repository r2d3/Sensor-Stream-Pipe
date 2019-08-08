#include "VideoUtils.h"

void avframeToMat(const AVFrame *frame, cv::Mat &image) {
    int width = frame->width;
    int height = frame->height;

    //std::cout << av_frame_get_color_range(frame) << " " << image.step1() << " " << image.step1(0) << std::endl;

    SwsContext *conversion;

    //TODO: this only works for 3 channel 8 bit color frames, make it work also for 1 channel 16 bit depth
    // Allocate the opencv mat and store its stride in a 1-element array
    image = cv::Mat(height, width, CV_8UC3);
    conversion = sws_getContext(width, height, (AVPixelFormat) frame->format, width, height, AV_PIX_FMT_BGR24,
                                SWS_FAST_BILINEAR, NULL, NULL, NULL);
    int cvLinesizes[1];
    cvLinesizes[0] = image.step1();

    // Convert the colour format and write directly to the opencv matrix
    sws_scale(conversion, frame->data, frame->linesize, 0, height, &image.data, cvLinesizes);
    sws_freeContext(conversion);
}

void avframeToMatGray(const AVFrame *frame, cv::Mat &image) {
    int width = frame->width;
    int height = frame->height;

    //std::cout << av_frame_get_color_range(frame) << " " << image.step1() << " " << image.step1(0) << std::endl;

    SwsContext *conversion;

    //TODO: this only works for 3 channel 8 bit color frames, make it work also for 1 channel 16 bit depth
    // Allocate the opencv mat and store its stride in a 1-element array
    image = cv::Mat(height, width, CV_16UC1);
    conversion = sws_getContext(width, height, (AVPixelFormat) frame->format, width, height, AV_PIX_FMT_GRAY16,
                                SWS_FAST_BILINEAR, NULL, NULL, NULL);
    int cvLinesizes[1];
    cvLinesizes[0] = image.step1();

    // Convert the colour format and write directly to the opencv matrix
    sws_scale(conversion, frame->data, frame->linesize, 0, height, &image.data, cvLinesizes);
    sws_freeContext(conversion);
}

void prepareDecodingStruct(FrameStruct &f, std::unordered_map<std::string, AVCodec *> &pCodecs,
                           std::unordered_map<std::string, AVCodecContext *> &pCodecContexts,
                           std::unordered_map<std::string, AVCodecParameters *> &pCodecParameters) {
    AVCodecParameters *pCodecParameter = f.codec_data.getParams();
    AVCodec *pCodec = avcodec_find_decoder(f.codec_data.getParams()->codec_id);
    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);

    if (!pCodecContext) {
        std::cerr << "failed to allocated memory for AVCodecContext" << std::endl;
        exit(1);
    }

    if (avcodec_parameters_to_context(pCodecContext, pCodecParameter) < 0) {
        std::cerr << ("failed to copy codec params to codec context") << std::endl;
        exit(1);
    }

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
        std::cerr << ("failed to open codec through avcodec_open2") << std::endl;
        exit(1);
    }

    pCodecs[f.streamId] = pCodec;
    pCodecContexts[f.streamId] = pCodecContext;
    pCodecParameters[f.streamId] = pCodecParameter;
}


cv::Mat getFloat(cv::Mat &input) {
    cv::Mat output;

    input.convertTo(output, CV_32FC1);

    return output / MAX_DEPTH_VALUE;

}

cv::Mat getUMat(cv::Mat &input) {
    cv::Mat outputF, outputU;
    double minVal, maxVal;

    input.convertTo(outputF, CV_32FC1);

    outputF = (outputF / MAX_DEPTH_VALUE) * 255;

    outputF.convertTo(outputU, CV_8UC1);

    return outputU;

}

std::vector<ushort> unique(const cv::Mat &input, bool sort) {

    std::vector<ushort> out;
    for (int y = 0; y < input.rows; ++y) {
        const ushort *row_ptr = input.ptr<ushort>(y);
        for (int x = 0; x < input.cols; ++x) {
            ushort value = row_ptr[x];

            if (std::find(out.begin(), out.end(), value) == out.end())
                out.push_back(value);
        }
    }

    if (sort)
        std::sort(out.begin(), out.end());

    return out;
}

void prepareGrayDepthFrame(cv::Mat &frame, AVFrame *pFrame) {
    for (uint y = 0; y < pFrame->height; y++) {
        for (uint x = 0; x < pFrame->width * 2; x++) {
            pFrame->data[0][y * pFrame->linesize[0] + x] = frame.at<float>(y, x / 2);
        }
    }
}


void prepareDepthFrame(cv::Mat &frame, AVFrame *pFrame) {
    for (uint y = 0; y < pFrame->height; y++) {
        for (uint x = 0; x < pFrame->width; x++) {
            pFrame->data[0][y * pFrame->linesize[0] + x] = frame.at<float>(y, x) * 255;
        }
    }

    for (uint y = 0; y < pFrame->height / 2; y++) {
        for (uint x = 0; x < pFrame->width / 2; x++) {
            pFrame->data[1][y * pFrame->linesize[1] + x] = 128;
            pFrame->data[2][y * pFrame->linesize[2] + x] = 128;
        }
    }
}

void prepareColorFrame(cv::Mat &frame, AVFrame *pFrame) {
    for (uint y = 0; y < pFrame->height; y++) {
        for (uint x = 0; x < pFrame->width; x++) {
            pFrame->data[0][y * pFrame->linesize[0] + x] = frame.at<cv::Vec3b>(y, x).val[0];
        }
    }
    for (uint y = 0; y < pFrame->height / 2; y++) {
        for (uint x = 0; x < pFrame->width / 2; x++) {
            pFrame->data[1][y * pFrame->linesize[1] + x] = frame.at<cv::Vec3b>(2 * y, 2 * x).val[1];
            pFrame->data[2][y * pFrame->linesize[2] + x] = frame.at<cv::Vec3b>(2 * y, 2 * x).val[2];
        }
    }

}

double getPSNR(const cv::Mat &I1, const cv::Mat &I2, int maxValue) {
    cv::Mat s1;
    absdiff(I1, I2, s1);       // |I1 - I2|
    s1.convertTo(s1, CV_32F);  // cannot make a square on 8 bits
    s1 = s1.mul(s1);           // |I1 - I2|^2

    cv::Scalar s = sum(s1);         // sum elements per channel

    double sse = s.val[0] + s.val[1] + s.val[2]; // sum channels
    if (sse <= 1e-10) { // for small values return zero
        return 0;
    } else {
        double mse = sse / (double) (I1.channels() * I1.cols * I1.rows);
        double psnr = 10.0 * log10((maxValue * maxValue) / mse);
        return psnr;
    }
}
