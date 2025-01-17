#pragma once
#include "data_items.h"
#include "ctl_app.h"
#include "camera_iface.h"

#include <cppcms/application.h>
#include <cppcms/http_context.h>
#include <cppcms/http_response.h>
#include <cppcms/url_dispatcher.h>
#include <cppcms/service.h>
#include <booster/log.h>

#include "server_sent_events.h"
#include "util.h"
namespace ols {
    class StackerControlApp : public ControlAppBase {
    public:
        StackerControlApp(cppcms::service &srv,
                          CameraInterface *iface,
                          std::string data_dir,
                          queue_pointer_type queue): 
            ControlAppBase(srv),
            cam_(iface),
            data_dir_(data_dir),
            queue_(queue)
        {
            stacked_path_ = data_dir_ + "/stacked";
            calibration_path_ = data_dir_ + "/calibration";
            dispatcher().map("POST","/start/?",&StackerControlApp::start,this);
            dispatcher().map("POST","/control/?",&StackerControlApp::control,this);
            dispatcher().map("POST","/stretch/?",&StackerControlApp::stretch,this);
            dispatcher().map("GET", "/status/?",&StackerControlApp::status,this);
        }
        void status()
        {
            response_["status"] = status_;
        }
        void control()
        {
            std::shared_ptr<StackerControl> cmd(new StackerControl());
            std::string op = content_.get<std::string>("operation");
            if(op == "pause") {
                cmd->op = StackerControl::ctl_pause;
                status_ = "paused";
            }
            else if(op == "save") {
                cmd->op = StackerControl::ctl_save;
            }
            else if(op == "resume") {
                cmd->op = StackerControl::ctl_resume;
                status_ = "stacking";
            }
            else if(op == "cancel"){
                cmd->op = StackerControl::ctl_cancel;
                status_ = "idle";
            }
            else
                throw std::runtime_error("Unknown operation " + op);
            queue_->push(cmd);
        }
        void stretch()
        {
            std::shared_ptr<StackerControl> cmd(new StackerControl());
            cmd->op = StackerControl::ctl_update;
            cmd->auto_stretch = content_.get("auto_stretch",cmd->auto_stretch);
            cmd->stretch_low = content_.get("stretch_low",cmd->stretch_low);
            cmd->stretch_high = content_.get("stretch_high",cmd->stretch_high);
            cmd->stretch_gamma = content_.get("stretch_gamma",cmd->stretch_gamma);
            queue_->push(cmd);
        }
        void start()
        {
            std::shared_ptr<StackerControl> cmd(new StackerControl());
            cmd->op = StackerControl::ctl_init;
            auto format = cam_->stream_format();
            cmd->mono = is_mono_stream(format.format);
            cmd->format = stream_type_to_str(cam_->stream_format().format);
            cmd->bin = cam_->stream_format().bin;
            cmd->width = format.width;
            cmd->height = format.height;
            cmd->calibration = content_.get("type","dso") == "calibration";
            cmd->name = content_.get<std::string>("name");
            cmd->save_inputs = content_.get("save_data",false);
            if(!cmd->calibration) {
                if(!cmd->name.empty())
                    cmd->name += "_";
                cmd->name += ftime("%Y%m%d_%H%M%S",time(nullptr));
                cmd->output_path = stacked_path_ + "/" + cmd->name; 
            }
            else {
                cmd->output_path = calibration_path_;
            }
            cmd->source_gamma = 1.0;
            {
                CamErrorCode e;
                std::unique_lock<std::recursive_mutex> guard(cam_->lock());
                auto opts = cam_->cam().supported_options(e);
                e.check();
                for(auto opt:opts) {
                    CamParam param = cam_->cam().get_parameter(opt,true,e);
                    e.check();
                    cmd->camera_config[opt] = param.cur_val;
                    if(opt == opt_gamma)
                        cmd->source_gamma = param.cur_val;
                }
            }
                
            cmd->lat = content_.get("location.lat",cmd->lat);
            cmd->lon = content_.get("location.lon",cmd->lon);
            cmd->ra = content_.get("target.ra",cmd->ra);
            cmd->de = content_.get("target.de",cmd->de);
            cmd->derotate_mirror = content_.get("image_flip",cmd->derotate_mirror);
            cmd->derotate = content_.get("field_derotation",cmd->derotate);
            cmd->rollback_on_pause = content_.get("rollback_on_pause",cmd->rollback_on_pause);
            cmd->darks_path = content_.get("darks",cmd->darks_path);
            cmd->flats_path = content_.get("flats",cmd->flats_path);
            cmd->dark_flats_path = content_.get("dark_flats",cmd->dark_flats_path);
            cmd->auto_stretch = content_.get("auto_stretch",cmd->auto_stretch);
            cmd->stretch_low = content_.get("stretch_low",cmd->stretch_low);
            cmd->stretch_high = content_.get("stretch_high",cmd->stretch_high);
            cmd->stretch_gamma = content_.get("stretch_gamma",cmd->stretch_gamma);
            cmd->remove_satellites = content_.get("remove_satellites",cmd->remove_satellites);

            if(!cmd->darks_path.empty())
                cmd->darks_path = calibration_path_ + "/" + cmd->darks_path + ".tiff";

            if(!cmd->flats_path.empty())
                cmd->flats_path = calibration_path_ + "/" + cmd->flats_path + ".tiff";

            if(!cmd->dark_flats_path.empty())
                cmd->dark_flats_path = calibration_path_ + "/" + cmd->dark_flats_path + ".tiff";

            status_ = "stacking";
            queue_->push(cmd);
        }
    private:
        CameraInterface *cam_;
        std::string data_dir_;
        std::string stacked_path_;
        std::string calibration_path_;
        queue_pointer_type queue_;
        std::string status_ = "idle";
    };

    class StackerStatsNotification: public cppcms::application {
    public:
        StackerStatsNotification(cppcms::service &srv):
            cppcms::application(srv)
        {
            stream_ = sse::bounded_event_queue::create(srv.get_io_service(),16);
        }
        void main(std::string /*url*/)
        {
            stream_->accept(release_context());
        }
        std::function<void(data_pointer_type)> get_callback()
        {
            booster::intrusive_ptr<StackerStatsNotification> self = this;
            return [=](data_pointer_type p) {
                self->stats_handler(p);
            };
        }
        // thread safe method
        void stats_handler(data_pointer_type stats)
        {
            service().post([=](){
                update_stats(stats);
            });
        }
        void update_stats(data_pointer_type p)
        {
            std::shared_ptr<StatsData> data = std::dynamic_pointer_cast<StatsData>(p);
            std::shared_ptr<ErrorNotificationData> error = std::dynamic_pointer_cast<ErrorNotificationData>(p);
            std::ostringstream ss;
            cppcms::json::value info;
            if(data) {
                info["type"] = "stats";
                info["stacked"] = data->stacked;
                info["missed"] = data->missed;
                info["dropped"] = data->dropped;
                info["since_saved_s" ] = data->since_saved_s;
                info["histogramm"] = data->histogramm;
            }
            else if(error) {
                info["type"] = "error";
                info["message"] = error->message;
                info["source"] = error->source;
            }
            else 
                return;
            ss << info;
            stream_->enqueue(ss.str());
        }
    private:
        std::shared_ptr<sse::bounded_event_queue> stream_;
    };
};

