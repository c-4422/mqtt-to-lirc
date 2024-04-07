/**************************************************************************************
 * Pioneer VSX-3300 MQTT Remote Control Program
 *      Need eclipse/paho libraries to compile this
 *      https://github.com/eclipse/paho.mqtt.cpp
 *
 *      Also need paho C libraries
 *      liblirc-dev
 *      libao-dev
 *      libsndfile-dev
 *
 *      On fedora you will need to add a shared library include file in:
 *      /etc/ld.so.conf.d/locallib64-x86_64.conf
 *      Contents of that file are:
 *      /usr/local/lib64/
 *      sudo ldconfig
 *
 *      This will allow the program to find the shared libraries it needs. The
 * config file for lirc is included as CU-A002 this file needs to be renamed and
 * copied to: sudo cp CU-A002.conf /etc/lirc/lircd.conf
 *
 *      After the config is loaded run the lircd service:
 *          sudo systemctl start lircd
 *
 *      At this point the program should be able to hook into lirc and work.
 *
 *      Build with:
 *        g++ -g -std=c++17 remote.cpp -lpaho-mqttpp3 -lpaho-mqtt3a
 * -llirc_client -lsndfile -lao -lpthread -o remote
 *
 * For cmake you need to build the C mqtt libraries to statically link:
 * cd paho.mqtt.c
 * cmake -DPAHO_WITH_SSL=TRUE -DPAHO_BUILD_DOCUMENTATION=TRUE
 * -DPAHO_BUILD_SAMPLES=TRUE -DPAHO_BUILD_STATIC=TRUE . cmake --build .
 *
 * To generate cmake files and build:
 * cmake . && make
 *
 * by c-4422
 * ***********************************************************************************/

#include "lirc_client.h"
#include <ao/ao.h>
#include <array>
#include <chrono>
#include <fstream>
#include <memory>
#include <mqtt/client.h> // Mosquitto client.
#include <mutex>
#include <ostream> // std::cout.
#include <signal.h>
#include <sndfile.h>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#define BUFFER_SIZE 8192

static constexpr auto DEBUG = true;
static int sSocketID;
static std::string sHomeAlarmAudioPath = "";
static std::string sAwayAlarmAudioPath = "";

enum CredKey {
  INVALID,
  IP,
  ID,
  USER,
  PASSWORD,
  SUBSCRIPTION,
  ALARM_AWAY,
  ALARM_HOME
};

struct Credentials {
  CredKey mKey;
  std::string_view mString;
};

// Define the core configuration keys as static constexpr so that they are
// statically compiled into the program, we don't want to construct strings at
// run time because it's unnecessary.
static constexpr std::array<Credentials, 7> CredWords = {
    {{CredKey::IP, "IP"},
     {CredKey::ID, "ID"},
     {CredKey::USER, "USER"},
     {CredKey::PASSWORD, "PASSWORD"},
     {CredKey::SUBSCRIPTION, "SUBSCRIPTION"},
     {CredKey::ALARM_AWAY, "ALARM_AWAY"},
     {CredKey::ALARM_HOME, "ALARM_HOME"}}};

struct AudioKeys {
  AudioKeys(std::string aKeyString, std::string aFilePath)
      : mKeyString(aKeyString), mFilePath(aFilePath) {}

  std::string mKeyString;
  std::string mFilePath;
};

static std::vector<AudioKeys> sAudioFiles = {};

// MQTT connection credentials these should be loaded in from the
// configuration file.
static std::string sIp = "";
static std::string sId = "";
static std::string sUser = "";
static std::string sPassword = "";
static std::string sSubscription = "";

/********************************
 * Audio Class
 * *****************************/

class Audio {
public:
  Audio();
  ~Audio();

  void PlayAudio(std::string aFilePath, bool aIsRepeat = false);
  const bool IsPlaying();
  void StopPlaying();
  void SoundAlarm(bool aIsAway);

private:
  void SetPlaying(bool aIsPlaying);
  void Play(std::string aFilePath, bool aIsRepeat);

  bool mIsPlaying = false;
  int mAoDriver;
  std::mutex mIsPlayingMutex;
};

static std::unique_ptr<Audio> sPlayer;
static std::unique_ptr<mqtt::async_client> sClient;

Audio::Audio() {
  ao_initialize();
  mAoDriver = ao_default_driver_id();
}

Audio::~Audio() { ao_shutdown(); }

void Audio::SetPlaying(const bool aIsPlaying) {
  std::lock_guard<std::mutex> gaurd(mIsPlayingMutex);
  mIsPlaying = aIsPlaying;
}

const bool Audio::IsPlaying() {
  std::lock_guard<std::mutex> gaurd(mIsPlayingMutex);
  return mIsPlaying;
}

void Audio::StopPlaying() { SetPlaying(false); }

void Audio::PlayAudio(std::string aFilePath, const bool aIsRepeat) {
  SetPlaying(true);
  Play(aFilePath, aIsRepeat);
}

void Audio::Play(std::string aFilePath, const bool aIsRepeat) {
  if (DEBUG == true) {
    std::cout << "Audio::PlayAudio called\n";
  }
  ao_device *device;
  ao_sample_format format;
  SF_INFO sfinfo;
  short *buffer;

  // Open media file
  SNDFILE *file = sf_open(aFilePath.c_str(), SFM_READ, &sfinfo);
  if (file == nullptr) {
    // Handle this gracefully don't exit the program.
    std::cout << "ERROR: Cannot open \"" << aFilePath << "\"!\n";
    SetPlaying(false);
    return;
  }

  switch (sfinfo.format & SF_FORMAT_SUBMASK) {
  case SF_FORMAT_PCM_16:
    format.bits = 16;
    break;
  case SF_FORMAT_PCM_24:
    format.bits = 24;
    break;
  case SF_FORMAT_PCM_32:
    format.bits = 32;
    break;
  case SF_FORMAT_PCM_S8:
    format.bits = 8;
    break;
  case SF_FORMAT_PCM_U8:
    format.bits = 8;
    break;
  default:
    format.bits = 16;
    break;
  }

  // Set output format and open output device
  format.rate = sfinfo.samplerate;
  format.channels = sfinfo.channels;
  format.byte_format = AO_FMT_NATIVE;
  format.matrix = 0;
  device = ao_open_live(mAoDriver, &format, NULL);

  // Play file and repeat if necessary
  buffer = new short[BUFFER_SIZE]();
  int read = 0;
  while ((read = sf_read_short(file, buffer, BUFFER_SIZE)) > 0 && IsPlaying()) {
    if (ao_play(device, (char *)buffer, (uint_32)(read * sizeof(short))) == 0) {
      if (DEBUG == true) {
        std::cout << "Error playing file";
      }
      SetPlaying(false);
    }
  }
  // Clean up
  sf_close(file);
  ao_close(device);
  delete[] buffer;

  if (aIsRepeat && IsPlaying()) {
    PlayAudio(aFilePath, aIsRepeat);
  }
  SetPlaying(false);
}

// This is the sound alarm function, It turns the volume down for a specified
// time. Then turns the volume up and plays either the away or home alarm
// sounds. This playback overrides all other playback requests until the alarm
// is dismissed.
void Audio::SoundAlarm(const bool aIsAway) {
  if (!IsPlaying()) {
    SetPlaying(true);
    // turn volume down
    if (sSocketID < 0) {
      std::cout
          << "ERROR: lirc socket is invalid cannot adjust stereo volume\n";
    } else {
      for (auto start = std::chrono::steady_clock::now(), now = start;
           now < start + std::chrono::seconds{15};
           now = std::chrono::steady_clock::now()) {
        lirc_send_one(sSocketID, "pioneer", "KEY_VOLUMEDOWN");
      }
      // turn up volume half way
      for (auto start = std::chrono::steady_clock::now(), now = start;
           now < start + std::chrono::seconds{5};
           now = std::chrono::steady_clock::now()) {
        lirc_send_one(sSocketID, "pioneer", "KEY_VOLUMEUP");
      }
    }

    const auto &alarmFilePath =
        aIsAway ? sAwayAlarmAudioPath : sHomeAlarmAudioPath;
    if (!alarmFilePath.empty()) {
      std::thread([this, aIsAway, alarmPath = alarmFilePath]() {
        // Start playing alarm sound on different thread
        Play(alarmPath, true);
      }).detach();
    } else {
      auto alarmString = aIsAway ? "ALARM_AWAY" : "ALARM_HOME";
      std::cout << "ERROR: " << alarmString
                << " has not been set in in the configuration.conf file\n";
    }
  }
}

CredKey GetStringKeyCredential(std::string &aKey) {
  for (auto compare : CredWords) {
    if (compare.mString == aKey) {
      return compare.mKey;
    }
  }
  return CredKey::INVALID;
}

/////////////////////////////////////////////////////////////////////////////

/**
 * Local callback & listener class for use with the client connection.
 * This is primarily intended to receive messages, but it will also monitor
 * the connection to the broker. If the connection is lost, it will attempt
 * to restore the connection and re-subscribe to the topic.
 */
class callback : public virtual mqtt::callback,
                 public virtual mqtt::iaction_listener

{
public:
  callback(mqtt::async_client &cli, mqtt::connect_options &connOpts)
      : mNumRetry(0), mClient(cli), mConnectionOpts(connOpts) {}

private:
  // Counter for the number of connection retries
  int mNumRetry;
  // The MQTT client
  mqtt::async_client &mClient;
  // Options to use if we need to reconnect
  mqtt::connect_options &mConnectionOpts;

  void reconnect() {
    try {
      mClient.connect(mConnectionOpts, nullptr, *this);
    } catch (const mqtt::exception &exc) {
      std::cerr << "Error: " << exc.what() << std::endl;
      exit(1);
    }
  }

  // Re-connection failure
  void on_failure(const mqtt::token &tok) override {
    std::cout << "Connection attempt failed" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    if (++mNumRetry > 50) {
      exit(1);
      std::cout << "ERROR: 50 retrys attempted exiting program!\n";
    }
    reconnect();
  }

  // (Re)connection success
  // Either this or connected() can be used for callbacks.
  void on_success(const mqtt::token &tok) override {}

  // (Re)connection success
  void connected(const std::string &cause) override {
    static constexpr int QualityOfService = 2;
    std::cout << "\nSubscribing to topic '" << sSubscription << "'\n";
    mClient.subscribe(sSubscription, QualityOfService);
  }

  // Callback for when the connection is lost.
  // This will initiate the attempt to manually reconnect.
  void connection_lost(const std::string &cause) override {
    std::cout << "\nConnection lost" << std::endl;
    if (!cause.empty())
      std::cout << "\tcause: " << cause << std::endl;

    std::cout << "Reconnecting..." << std::endl;
    mNumRetry = 0;
    reconnect();
  }

  // Callback for when a message arrives.
  void message_arrived(mqtt::const_message_ptr msg) override {
    if (DEBUG == true) {
      std::cout << "Message arrived" << std::endl;
      std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
      std::cout << "\tpayload: '" << msg->to_string() << "'\n" << std::endl;
    }

    std::string messageString = msg->to_string();
    auto isAudioFile = [&messageString](AudioKeys &aAudioKey) {
      return aAudioKey.mKeyString == messageString;
    };

    if (messageString == "ALARM_AWAY" || messageString == "ALARM_HOME") {
      sPlayer->SoundAlarm(messageString == "ALARM_AWAY");
    } else if (messageString == "ALARM_CLEAR") {
      sPlayer->StopPlaying();
    } else if (const auto itr = std::find_if(sAudioFiles.begin(),
                                             sAudioFiles.end(), isAudioFile);
               itr != sAudioFiles.end()) {
      // If we have any audio files matching the message string from the
      // server play it.
      sPlayer->StopPlaying();
      sPlayer->PlayAudio(itr->mFilePath);
    } else if (sSocketID < 0 || lirc_send_one(sSocketID, "pioneer",
                                              messageString.c_str()) == -1) {
      std::cout << "Error: " << messageString << " is not a valid command\n";
    }
  }

  void delivery_complete(mqtt::delivery_token_ptr token) override {}
};

/////////////////////////////////////////////////////////////////////////////

void ShutdownConnection() {
  try {
    std::cout << "\nDisconnecting from the MQTT server..." << std::flush;
    sClient->disconnect()->wait();
    std::cout << "OK" << std::endl;
    exit(EXIT_SUCCESS);
  } catch (const mqtt::exception &exc) {
    std::cerr << exc << std::endl;
    exit(EXIT_FAILURE);
  }
}

void HandleSignalStop(int aSignal) {
  // OS has asked the system to stop. Gracefully shut down mqtt client.
  std::cout << "Recieved OS stop, stopping program.\n";
  ShutdownConnection();
}

int main() {
  // Handle signals from the OS to stop
  struct sigaction signalAction;
  signalAction.sa_handler = &HandleSignalStop;
  signalAction.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &signalAction, NULL);
  sigaction(SIGINT, &signalAction, NULL);

  // Open the configuration file
  std::ifstream confFile;

  confFile.open("configuration.conf");
  if (!confFile.is_open()) {
    std::cout << "ERROR: Cannot open \"configuration.conf\"!\n";
  }
  std::string lineString;
  while (std::getline(confFile, lineString)) {
    // Check if the line is a comment, if the line starts with a # (ignoring
    // whitespace) it's a comment
    std::stringstream lineStream(lineString);
    char firstNoWhiteSpaceChar;
    lineStream >> firstNoWhiteSpaceChar;
    if ((lineStream) || (firstNoWhiteSpaceChar != '#')) {
      std::string::size_type pos = lineString.find('=');
      if (pos != std::string::npos) {
        // Look for the equal sign if it's not there then it is not a valid
        // config option.
        auto key = lineString.substr(0, pos);
        auto parameter = lineString.substr(pos + 1, lineString.size());
        switch (GetStringKeyCredential(key)) {
        case CredKey::IP:
          sIp = parameter;
          break;
        case CredKey::ID:
          sId = parameter;
          break;
        case CredKey::USER:
          sUser = parameter;
          break;
        case CredKey::PASSWORD:
          sPassword = parameter;
          break;
        case CredKey::SUBSCRIPTION:
          sSubscription = parameter;
          break;
        case CredKey::ALARM_AWAY:
          sAwayAlarmAudioPath = parameter;
          break;
        case CredKey::ALARM_HOME:
          sHomeAlarmAudioPath = parameter;
          break;
        default:
          sAudioFiles.emplace_back(AudioKeys(key, parameter));
          break;
        }
      }
    }
  }

  if (sSubscription.empty() || sIp.empty() || sId.empty()) {
    // Check if credentials are empty. If so bail early.
    std::cout << "ERROR: Invalid IP, ID or Subscription topic\n";
    return 1;
  }

  sClient = std::make_unique<mqtt::async_client>(sIp, sId);

  mqtt::connect_options connOpts;
  connOpts.set_clean_session(false);
  connOpts.set_user_name(mqtt::string_ref(sUser));
  connOpts.set_password(mqtt::binary_ref(sPassword));

  // Install the callback(s) before connecting.
  callback cb(*sClient, connOpts);
  sClient->set_callback(cb);

  // Get lirc socket ID and start Audio class
  sSocketID = lirc_get_local_socket(NULL, 0);
  if (sSocketID < 0) {
    std::cout << "ERROR: could not establish LIRC socket ID\n";
    std::cout << "liblirc-dev must be installed.\n";
  }
  sPlayer = std::make_unique<Audio>();

  try {
    std::cout << "Connecting to the MQTT server..." << std::flush;
    sClient->connect(connOpts, nullptr, cb);
  } catch (const mqtt::exception &exc) {
    std::cerr << "\nERROR: Unable to connect to MQTT server: '" << sIp << "'"
              << exc << std::endl;
    return 1;
  }

  // Just block till user tells us to quit.
  while (std::tolower(std::cin.get()) != 'q')
    ;

  ShutdownConnection();

  return 0;
}
