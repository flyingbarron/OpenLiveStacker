#include "camera.h"
#include <dlfcn.h>
#include <algorithm>

extern "C" typedef ols::CameraDriver *(*cam_generator_ptr_type)(int);
extern "C" typedef int (*cam_config_ptr_type)(char const *);

namespace ols {

CamError::CamError(std::string const &msg) : std::runtime_error(msg)
{
}

static std::vector<std::string> driver_names;
static std::vector<cam_generator_ptr_type> driver_calls;

void CameraDriver::load_driver(std::string const &name,std::string base_path,char const *opt)
{
    if(std::find(driver_names.begin(),driver_names.end(),name) != driver_names.end())
        return;
    if(!base_path.empty())
        base_path += "/";
    void *h = dlopen((base_path + "libols_driver_" + name + ".so").c_str(),RTLD_LAZY | RTLD_GLOBAL);
    if(!h)
        throw CamError("Failed to load driver " + name);
    void *func = dlsym(h,("ols_get_" + name + "_driver").c_str());
    if(!func) {
        dlclose(h);
        throw CamError("Failed to find driver entry for " + name);
    }
    if(opt) {
        void *opt_func = dlsym(h,("ols_set_" + name + "_driver_config").c_str());
        if(!opt_func) {
            dlclose(h);
            throw CamError("Failed to find driver config entry for " + name);
        }
        cam_config_ptr_type config = reinterpret_cast<cam_config_ptr_type>(opt_func);
        if(config(opt)!=0)
            throw CamError("Failed to config driver for " + name);
    }

    driver_calls.insert(driver_calls.begin(),reinterpret_cast<cam_generator_ptr_type>(func));
    driver_names.insert(driver_names.begin(),name);
}

std::vector<std::string> CameraDriver::drivers()
{
    return driver_names;
}

std::unique_ptr<CameraDriver> CameraDriver::get(int id,int external_option)
{
    if((unsigned)id >= driver_calls.size())
        throw CamError("Invalid driver id"); 
    std::unique_ptr<CameraDriver> r(driver_calls.at(id)(external_option));
    if(!r)
        throw CamError("Failed to load camera " + std::to_string(id));
    return r;
}
std::ostream &operator<<(std::ostream &out,CamStreamFormat const &fmt)
{
    switch(fmt.format) {
    case stream_yuv2: out << "YUV2"; break;
    case stream_mjpeg: out << "MJPEG"; break;
    case stream_rgb24: out << "RGB24"; break;
    case stream_rgb48: out << "RGB48"; break;
    case stream_raw8:  out << "RAW8"; break;
    case stream_raw16:  out << "RAW16"; break;
    case stream_mono8:  out << "MONO8"; break;
    case stream_mono16:  out << "MONO16"; break;
    default: out << "Unknown";
    }
    out << ":" << fmt.width << "x" << fmt.height <<"@" << fmt.framerate;
    return out;
}

static char const *option_string_ids[] = {
    "auto_exp", "auto_wb", "exp", "wb", "wb_r", "wb_b", "gain", "gamma", "brightness","contrast", "temperature", "cooler_target","cooler_on", "fan_on", "cooler_power","live_stretch"
};
static char const *option_names[] = {
    "Auto Exp.", "Auto WB", "Exp.", "WB", "WB Red", "WB Blue", "Gain", "Gamma", "Bright.", "Contr.", "Temp.", "Cooler Tgt.", "Cooler", "Fan","Cooler Pwr.","Auto Str."
};

std::string bayer_type_to_str(CamBayerType bayer)
{
    switch(bayer) {
    case bayer_na: return "NA";
    case bayer_rg: return "RGGB";
    case bayer_gr: return "GRBG";
    case bayer_bg: return "BGGR";
    case bayer_gb: return "GBRG";
    };
    throw CamError("Invalid format");
}

CamBayerType bayer_type_from_str(std::string const &bayer)
{
    if(bayer == "NA") return bayer_na;
    if(bayer == "RGGB") return bayer_rg;
    if(bayer == "GRBG") return bayer_gr;
    if(bayer == "BGGR") return bayer_bg;
    if(bayer == "GBRG") return bayer_gb;
    throw CamError("Invalid bayer format " + bayer);
}

std::string cam_option_id_to_string_id(CamOptionId id)
{
    size_t nid = id;
    if(nid >= sizeof(option_string_ids)/sizeof(option_string_ids[0]))
        throw CamError("Invalid option id");
    return option_string_ids[nid];
}

std::string stream_type_to_str(CamStreamType s)
{
    switch(s) {
    case stream_yuv2: return "yuv2";
    case stream_rgb24: return "rgb24";
    case stream_rgb48: return "rgb48";
    case stream_raw8: return "raw8";
    case stream_raw16: return "raw16";
    case stream_mono8: return "mono8";
    case stream_mono16: return "mono16";
    case stream_mjpeg: return "mjpeg";
    default:
        throw CamError("Invalid Stream type");
    }
}

CamStreamType stream_type_from_str(std::string s)
{
    if(s=="mjpeg")
        return stream_mjpeg;
    if(s=="yuv2")
        return stream_yuv2;
    if(s=="rgb24")
        return stream_rgb24;
    if(s=="rgb48")
        return stream_rgb48;
    if(s=="raw8")
        return stream_raw8;
    if(s=="raw16")
        return stream_raw16;
    if(s=="mono8")
        return stream_mono8;
    if(s=="mono16")
        return stream_mono16;
    throw CamError("Invalid stream type " + s);
}


static char const *cam_option_type_names[]={"bool","number","msec","percent","kelvin","celsius"};

std::string cam_option_type_to_str(CamOptionType type)
{
    unsigned id = type;
    if(id >= sizeof(cam_option_type_names)/sizeof(cam_option_type_names[0]))
        throw CamError("Invalid type");
    return cam_option_type_names[id];
}
CamOptionType cam_option_type_from_str(std::string const &name)
{
    for(unsigned i=0;i<sizeof(cam_option_type_names)/sizeof(cam_option_type_names[0]);i++) {
        if(cam_option_type_names[i] == name) {
            return static_cast<CamOptionType>(i);
        }
    }
    throw CamError("Invalid type:" + name);
}


std::string cam_option_id_to_name(CamOptionId id)
{
    size_t nid = id;
    if(nid >= sizeof(option_names)/sizeof(option_names[0]))
        throw CamError("Invalid option id");
    return option_names[nid];
}

CamOptionId cam_option_id_from_string_id(std::string const &name)
{
    for(size_t i=0;i<sizeof(option_string_ids)/sizeof(option_string_ids[0]);i++)
        if(name == option_string_ids[i])
            return static_cast<CamOptionId>(i);
    throw CamError("Invalid option id");
}


} // ols
