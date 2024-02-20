#include "windows.h"
#include <stdio.h>
#include <stdint.h>

// all of those were revealed to me in a dream
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
typedef bool(__thiscall* InitCodec_t)(void* this_, int quality);
typedef int(__thiscall* Compress_t)(void* this_, const char* pUncompressed, int nSamples, char* pCompressed, int maxCompressedBytes, bool bFinal);
typedef int(__thiscall* Decompress_t)(void* this_, const char* pCompressed, int compressedBytes, char* pUncompressed, int maxUncompressedBytes);

// header from some random github gist
typedef struct wav_header {
    // RIFF Header
    char riff_header[4]; // Contains "RIFF"
    int wav_size; // Size of the wav portion of the file, which follows the first 8 bytes. File size - 8
    char wave_header[4]; // Contains "WAVE"

    // Format Header
    char fmt_header[4]; // Contains "fmt " (includes trailing space)
    int fmt_chunk_size; // Should be 16 for PCM
    short audio_format; // Should be 1 for PCM. 3 for IEEE Float
    short num_channels;
    int sample_rate;
    int byte_rate; // Number of bytes per second. sample_rate * num_channels * Bytes Per Sample
    short sample_alignment; // num_channels * Bytes Per Sample
    short bit_depth; // Number of bits per sample

    // Data
    char data_header[4]; // Contains "data"
    int data_bytes; // Number of bytes in data. Number of samples * num_channels * sample byte size
    // uint8_t bytes[]; // Remainder of wave file is bytes
} wav_header;

#define default_dll ".\\vaudio_speex.dll\0"
#define default_quality 4
#define default_outfile ".\\rawout.wav\0"

int main(int argc, char* argv[]) {
    char dll[MAX_PATH];
    strcpy (dll, default_dll);
    int quality = default_quality;
    char outfile[MAX_PATH];
    char input[MAX_PATH];
    strcpy(outfile, default_outfile);
    strcpy(input, ".\\in.wav");
    for (int i = 0; i < argc; i++) {
        if (_stricmp("-dll", argv[i]) == 0) {
            strcpy_s(dll, argv[i + 1]);
        }
        else if (_stricmp("-q", argv[i]) == 0) {
            quality = atoi(argv[i + 1]);
        }
        else if (_stricmp("-out", argv[i]) == 0) {
            strcpy_s(outfile, argv[i + 1]);
        }
        else if (_stricmp("-in", argv[i]) == 0) {
            strcpy_s(input, argv[i + 1]);
        }
    }
    if (!input) {
        printf("no input specified\n");
        return -1;
    }
	FILE* f = NULL;
    wav_header header{};
	fopen_s(&f, input, "rb");
    if (!f) {
        printf("couldn't load input file!\n");
        return -1;
    }

    if (!fread(&header, sizeof(wav_header), 1, f)) {
        printf("couldn't read main header!\n");
        return -1;
    }

    if (header.num_channels != 1 || header.bit_depth != 16) {
        printf("source engine voice chat can take only mono in 16bit!\n");
        return -1;
    }

    char* wavedata = (char*)malloc(header.data_bytes);
    if (!wavedata) {
        printf("couldn't alloc wavedata\n");
        return -1;
    }
    fread(wavedata, header.data_bytes, 1, f);

    // now the codec fuckery begins!!
    HMODULE modulebase = LoadLibrary(dll);
    if (!modulebase) {
        printf("couldn't load vaudio\n");
        return -1;
    }
    printf("loaded dll %s\n", dll);
    printf("module base @ %p\n", modulebase);
    
    // this blowssssssssssssssssssssssssssssssssssssssssssss
    char _nodll[MAX_PATH];
    strcpy(_nodll, dll);
    _nodll[strlen(_nodll) - 4] = '\0';
    char* nodll = strstr(_nodll, "vaudio_");

    printf("presumed interface name: %s\n", nodll);
    void* iface = ((CreateInterfaceFn)GetProcAddress(modulebase, "CreateInterface"))(nodll, NULL);
    if (!iface) {
        printf("couldn't init iface\n");
        return -1;
    }

    printf("interface @ %p\n", iface);

    void** vtbl = *(void***)iface;
    InitCodec_t Init = (InitCodec_t)vtbl[1];
    Compress_t Compress = (Compress_t)vtbl[3];
    Decompress_t Decompress = (Decompress_t)vtbl[4];

    if (!Init(iface, quality)) {
        printf("rip (could not initialize codec)\n");
        return -1;
    }

    int samples = header.data_bytes / (header.num_channels * header.bit_depth / 8);

    char* outdata = (char*)malloc(header.data_bytes);
    char* decompressed_data = (char*)malloc(header.data_bytes);
    if (!outdata || !decompressed_data) {
        //hi
        printf("oops\n");
        return -1;
    }

    int c = Compress(iface, wavedata, samples, outdata, header.data_bytes, false);
    int d = Decompress(iface, outdata, c, decompressed_data, header.data_bytes);
    //__asm { int 3 } // FUCK!!!
    printf("%i %i\n", c, d);
    FILE* f_out;
    fopen_s(&f_out, outfile, "wb");
    if (!f_out) {
        printf("couldn't open output stream\n");
        return -1;
    }

    // try writing a wav
    wav_header header_out{};
    memcpy(&header_out.riff_header, "RIFF", 4);
    header_out.wav_size = d + 36;
    memcpy(&header_out.wave_header, "WAVE", 4);
    memcpy(&header_out.fmt_header, "fmt ", 4);
    header_out.fmt_chunk_size = 16;
    header_out.audio_format = 1;
    header_out.num_channels = 1;
    header_out.sample_rate = header.sample_rate;
    header_out.sample_alignment = 16;
    header_out.bit_depth = 16;
    memcpy(&header_out.data_header, "data", 4);
    header_out.data_bytes = d;

    if (!fwrite(&header_out, sizeof(wav_header), 1, f_out)) {
        printf("couldn't write to file\n");
        return -1;
    }
    fwrite(decompressed_data, d, 1, f_out);
    //if (!fwrite(decompressed_data, d, 1, f_out)) {
    //    printf("couldn't write to file\n");
    //    return -1;
    //}

    free(outdata);
    free(decompressed_data);
    free(wavedata);
}