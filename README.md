26年 一月份才开工. 现在没空

到时候我会设计成这种风格
```c++
NbFFmpeg().input("a.mp4").output("b.mp4").run();
```
等效于
```shell
ffmpeg.exe -i a.mp4 b.mp4
```


# nbffmpeg 函数接口

```c++
enum class FrameType { Video, Audio };

using PacketCallback = std::function<void(AVPacket* pkt, FrameType type)>;
using FrameCallback = std::function<void(AVFrame* frame, FrameType type)>;
using LogCallback = std::function<void(const std::string& msg)>;
using ErrorCallback = std::function<void(int code, const std::string& msg)>;

class NbFFmpeg {
    nbFFmpeg& input(const std::string& path);
    nbFFmpeg& output(const std::string& path);
    
    nbFFmpeg& onPacketRead(PacketCallback cb);
    nbFFmpeg& onFrameDecoded(FrameCallback cb);
    nbFFmpeg& onPacketEncoded(PacketCallback cb);
    
    int pushPacket(AVPacket* pkt, FrameType type, int inputIndex = 0);
    int pushFrame(AVFrame* frame, FrameType type, int inputIndex = 0);
    
    nbFFmpeg& size(int width, int height);
    nbFFmpeg& pixFmt(const std::string& format);
    nbFFmpeg& sampleRate(int rate);
    nbFFmpeg& channels(int n);
    nbFFmpeg& codec(const std::string& codecName);
    
    nbFFmpeg& option(const std::string& key, const std::string& value);
    
    nbFFmpeg& onLog(LogCallback cb);
    nbFFmpeg& onError(ErrorCallback cb);
    
    int run();
    int runAsync();
    void wait();
    void stop();
};
```

到时候这五个线程组合
1. 输入读取线程
2. 解码线程
3. 完美转发线程
4. 编码线程
5. 输出写入线程