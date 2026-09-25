#pragma once


namespace engine::networking {

    enum class NetworkError {
        None = 0,
        InvalidRequestSize = 1,
        HandlerError = 2,
        InvalidResponseSize = 3,
        SendFailed = 4,
        ReceiveFailed = 5
    };

    enum class ReceivedStatus {
        Success, 
        NoMessage,
        InvalidSize
    };

}