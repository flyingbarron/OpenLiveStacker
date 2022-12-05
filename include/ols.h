#pragma once
#include "camera.h"
#include "data_items.h"
#include "video_generator.h"
#include <cppcms/service.h>
#include <thread>

namespace ols {


    class OpenLiveStacker {
    public:
        OpenLiveStacker();
        ~OpenLiveStacker();

        int http_port = 8080;
        std::string http_ip = "0.0.0.0";
        std::string document_root = "www-data";

        void init(std::string driver,int id=0);
        void run();
        void shutdown();

    private:
        void stop();
        void handle_video_frame(CamFrame const &cf);

        /// Data Queues

        queue_pointer_type converter_queue_         = std::shared_ptr<queue_type>(new queue_type());
        queue_pointer_type stacker_queue_           = std::shared_ptr<queue_type>(new queue_type());
        queue_pointer_type post_processing_queue_   = std::shared_ptr<queue_type>(new queue_type());
        queue_pointer_type stacked_display_queue_   = std::shared_ptr<queue_type>(new queue_type());
        queue_pointer_type video_display_queue_     = std::shared_ptr<queue_type>(new queue_type());
        queue_pointer_type data_save_queue_         = std::shared_ptr<queue_type>(new queue_type());
        queue_pointer_type stacking_progress_queue_ = std::shared_ptr<queue_type>(new queue_type());

        /// Flow

        /// Camera |-> data_save_queue_ -> DebugFrameSaver
        ///        |-> video_display_queue_ -> RawVideoGenerator 
        ///        |-> converter_queue_ -> Frame2Mat -> stacker_queue_ -> Stacker |-> post_processing_queue_ -> PostProcess -> stacked_display_queue_ -> StackedVideoGenerator
        ///                                                                       |-> stacking_progress_queue_ -> PublishStats  
        
        std::mutex camera_lock_;
        std::unique_ptr<Camera> camera_;
        std::unique_ptr<CameraDriver> driver_;

        std::unique_ptr<VideoGenerator> video_generator_;
        booster::intrusive_ptr<VideoGeneratorApp> video_generator_app_;
        std::thread video_generator_thread_;

        //std::unique_ptr<Stacker> stacker_;
        //std::thread stacker_thread_;
        
        std::shared_ptr<cppcms::service> web_service_;

    };
};
