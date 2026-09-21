#pragma once


namespace engine::networking {

    enum class NetworkError {
        None = 0,
        InvalidRequestSize = 1,
        HandlerError = 2,
        InvalidResponseSize = 3,
        ReceiveFailed = 4
    };

    enum class ReceivedStatus {
        Success, 
        NoMessage,
        InvalidSize
    };

    template <typename T>
    struct ResponsePacket
    {
        NetworkError errorCode{NetworkError::None};
        T data{};
    };

}