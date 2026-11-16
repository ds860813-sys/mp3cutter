#include <iostream>

#define wWinMain AppEntryPoint
#include "../main.cpp"
#undef wWinMain

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcout << L"usage: core_smoke.exe <source.mp3> <target.mp3>\n";
        return 2;
    }
    const std::wstring source = argv[1];
    const std::wstring target =
        argc >= 3 ? argv[2]
                  : std::wstring(L"bin\\core-smoke-output.mp3");
    if (!BASS_Init(0, 48000, 0, nullptr, nullptr) &&
        !BASS_Init(-1, 44100, 0, nullptr, nullptr)) {
        std::wcout << L"BASS_Init failed: " << BASS_ErrorGetCode() << L"\n";
        return 1;
    }

    std::atomic_bool cancel{false};
    WaveView wave;
    std::wstring error;
    const bool waveOk =
        CreateDecodeWaveform(source, cancel, &wave, &error);
    std::wcout << L"wave=" << (waveOk ? L"ok" : L"fail")
               << L" frames=" << wave.totalFrames
               << L" columns=" << wave.lo.size()
               << L" rate=" << wave.sampleRate
               << L" duration=" << wave.duration;
    if (!error.empty()) std::wcout << L" error=" << error;
    std::wcout << L"\n";
    if (!waveOk) {
        BASS_Free();
        return 1;
    }

    const double start = argc >= 4 ? std::wcstod(argv[3], nullptr) : 0.13;
    const double stop = argc >= 5 ? std::wcstod(argv[4], nullptr) : 0.37;
    ExportResult result =
        ExportMp3Range(source, target, start, stop, cancel);
    std::wcout << L"export=" << (result.ok ? L"ok" : L"fail")
               << L" duration=" << result.duration
               << L" start=" << result.startFraction
               << L" stop=" << result.stopFraction;
    if (!result.error.empty()) std::wcout << L" error=" << result.error;
    std::wcout << L"\n";
    if (!result.ok) {
        BASS_Free();
        return 1;
    }

    HSTREAM verify = BASS_StreamCreateFile(
        FALSE, target.c_str(), 0, 0,
        BASS_UNICODE | BASS_STREAM_DECODE | BASS_STREAM_PRESCAN);
    if (!verify) {
        std::wcout << L"BASS could not open output: "
                   << BASS_ErrorGetCode() << L"\n";
        BASS_Free();
        return 1;
    }
    const QWORD bytes = BASS_ChannelGetLength(verify, BASS_POS_BYTE);
    const double seconds = BASS_ChannelBytes2Seconds(verify, bytes);
    BASS_StreamFree(verify);
    BASS_Free();
    std::wcout << L"verified=ok pcm_bytes=" << bytes
               << L" seconds=" << seconds << L"\n";
    return std::abs(seconds - result.duration) < 0.02 ? 0 : 1;
}
