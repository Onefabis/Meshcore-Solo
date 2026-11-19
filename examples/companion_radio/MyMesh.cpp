#include "MyMesh.h"
#ifdef PRIVATE_STATUS_REPORTER
#include "TrackerProtocol.h"
#include "TrackerAudio.h"
#endif
#include "MsgExpand.h"
#include "ui-core/BotCommands.h"
#include "GeoUtils.h"
#include "Features.h"

#include <Arduino.h> // needed for PlatformIO
#ifdef ENABLE_WIFI_INTERFACE
#include <WiFi.h>
#endif
#include <Mesh.h>

#include <helpers/UTF8Helpers.h>
#if defined(DISPLAY_CLASS) && defined(ENABLE_SCREENSHOT)
#include "helpers/ui/DisplayDriver.h"
#include "UITask.h"   // screenshot debug command reads the UI's display
#endif

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2
#define CMD_SEND_CHANNEL_TXT_MSG      3
#define CMD_GET_CONTACTS              4 // with optional 'since' (for efficient sync)
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_ADD_UPDATE_CONTACT        9
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_RESET_PATH                13
#define CMD_SET_ADVERT_LATLON         14
#define CMD_REMOVE_CONTACT            15
#define CMD_SHARE_CONTACT             16
#define CMD_EXPORT_CONTACT            17
#define CMD_IMPORT_CONTACT            18
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20   // was CMD_GET_BATTERY_VOLTAGE
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QUERY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_SEND_RAW_DATA             25
#define CMD_SEND_LOGIN                26
#define CMD_SEND_STATUS_REQ           27
#define CMD_HAS_CONNECTION            28
#define CMD_LOGOUT                    29 // 'Disconnect'
#define CMD_GET_CONTACT_BY_KEY        30
#define CMD_GET_CHANNEL               31
#define CMD_SET_CHANNEL               32
#define CMD_SIGN_START                33
#define CMD_SIGN_DATA                 34
#define CMD_SIGN_FINISH               35
#define CMD_SEND_TRACE_PATH           36
#define CMD_SET_DEVICE_PIN            37
#define CMD_SET_OTHER_PARAMS          38
#define CMD_SEND_TELEMETRY_REQ        39  // can deprecate this
#define CMD_GET_CUSTOM_VARS           40
#define CMD_SET_CUSTOM_VAR            41
#define CMD_GET_ADVERT_PATH           42
#define CMD_GET_TUNING_PARAMS         43
// NOTE: CMD range 44..49 parked, potentially for WiFi operations
#define CMD_SEND_BINARY_REQ           50
#define CMD_FACTORY_RESET             51
#define CMD_SEND_PATH_DISCOVERY_REQ   52
#define CMD_SET_FLOOD_SCOPE_KEY       54   // v8+
#define CMD_SEND_CONTROL_DATA         55   // v8+
#define CMD_GET_STATS                 56   // v8+, second byte is stats type
#define CMD_SEND_ANON_REQ             57
#define CMD_SET_AUTOADD_CONFIG        58
#define CMD_GET_AUTOADD_CONFIG        59
#define CMD_GET_ALLOWED_REPEAT_FREQ   60
#define CMD_SET_PATH_HASH_MODE        61
#define CMD_SEND_CHANNEL_DATA         62
#define CMD_SET_DEFAULT_FLOOD_SCOPE   63
#define CMD_GET_DEFAULT_FLOOD_SCOPE   64
#define CMD_SEND_RAW_PACKET           65
#define CMD_RUN_CLI_COMMAND           66  // v14+
#if FEAT_ROUTES
#define CMD_ROUTE                      0x7D   // planned routes: list, upload, delete (Solo; MyMeshRoutes.h)
#endif
#ifdef ENABLE_SCREENSHOT
#define CMD_GET_SCREENSHOT             0x7E   // Request screenshot from display (Solo; 66 is upstream's CLI)
#endif

// Stats sub-types for CMD_GET_STATS
#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS             2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2  // first reply to CMD_GET_CONTACTS
#define RESP_CODE_CONTACT             3  // multiple of these (after CMD_GET_CONTACTS)
#define RESP_CODE_END_OF_CONTACTS     4  // last reply to CMD_GET_CONTACTS
#define RESP_CODE_SELF_INFO           5  // reply to CMD_APP_START
#define RESP_CODE_SENT                6  // reply to CMD_SEND_TXT_MSG
#define RESP_CODE_CONTACT_MSG_RECV    7  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CHANNEL_MSG_RECV    8  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CURR_TIME           9  // a reply to CMD_GET_DEVICE_TIME
#define RESP_CODE_NO_MORE_MESSAGES    10 // a reply to CMD_SYNC_NEXT_MESSAGE
#define RESP_CODE_EXPORT_CONTACT      11
#define RESP_CODE_BATT_AND_STORAGE    12 // a reply to a CMD_GET_BATT_AND_STORAGE
#define RESP_CODE_DEVICE_INFO         13 // a reply to CMD_DEVICE_QUERY
#define RESP_CODE_PRIVATE_KEY         14 // a reply to CMD_EXPORT_PRIVATE_KEY
#define RESP_CODE_DISABLED            15
#define RESP_CODE_CONTACT_MSG_RECV_V3 16 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_MSG_RECV_V3 17 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_INFO        18 // a reply to CMD_GET_CHANNEL
#define RESP_CODE_SIGN_START          19
#define RESP_CODE_SIGNATURE           20
#define RESP_CODE_CUSTOM_VARS         21
#define RESP_CODE_ADVERT_PATH         22
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24   // v8+, second byte is stats type
#define RESP_CODE_AUTOADD_CONFIG      25
#define RESP_ALLOWED_REPEAT_FREQ      26
#define RESP_CODE_CHANNEL_DATA_RECV   27
#define RESP_CODE_DEFAULT_FLOOD_SCOPE 28
#define RESP_CODE_CLI_REPLY           29  // v14+, a reply to CMD_RUN_CLI_COMMAND
#ifdef ENABLE_SCREENSHOT
#define RESP_CODE_SCREENSHOT          0x7E  // reply to CMD_GET_SCREENSHOT (Solo; 29 is upstream's CLI reply)
#define SCREENSHOT_TYPE_RGB565          2   // display_type of a colour screen's frame
#endif

#define MAX_CHANNEL_DATA_LENGTH       (MAX_FRAME_SIZE - 9)

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250
#define LAZY_CONTACTS_WRITE_DELAY       5000
#define LAZY_PATHS_WRITE_DELAY          60000   // a path learned (a DM's ACK brings one back): no hurry
#define USER_QUIET_MILLIS               15000   // the contacts write stalls the loop: not while the keys are in use
#define RTC_SAVE_INTERVAL_MILLIS        (30 * 60 * 1000UL)   // the time restored at boot after a reset or a flash

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

// these are _pushed_ to client app at any time
#define PUSH_CODE_ADVERT                0x80
#define PUSH_CODE_PATH_UPDATED          0x81
#define PUSH_CODE_SEND_CONFIRMED        0x82
#define PUSH_CODE_MSG_WAITING           0x83
#define PUSH_CODE_RAW_DATA              0x84
#define PUSH_CODE_LOGIN_SUCCESS         0x85
#define PUSH_CODE_LOGIN_FAIL            0x86
#define PUSH_CODE_STATUS_RESPONSE       0x87
#define PUSH_CODE_LOG_RX_DATA           0x88
#define PUSH_CODE_TRACE_DATA            0x89
#define PUSH_CODE_NEW_ADVERT            0x8A
#define PUSH_CODE_TELEMETRY_RESPONSE    0x8B
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define PUSH_CODE_PATH_DISCOVERY_RESPONSE 0x8D
#define PUSH_CODE_CONTROL_DATA          0x8E   // v8+
#define PUSH_CODE_CONTACT_DELETED       0x8F // used to notify client app of deleted contact when overwriting oldest
#define PUSH_CODE_CONTACTS_FULL         0x90 // used to notify client app that contacts storage is full

#define ERR_CODE_UNSUPPORTED_CMD        1
#define ERR_CODE_NOT_FOUND              2
#define ERR_CODE_TABLE_FULL             3
#define ERR_CODE_BAD_STATE              4
#define ERR_CODE_FILE_IO_ERROR          5
#define ERR_CODE_ILLEGAL_ARG            6

#define MAX_SIGN_DATA_LEN               (8 * 1024) // 8K

// Auto-add config bitmask
// Bit 0: If set, overwrite oldest non-favourite contact when contacts file is full
// Bits 1-4: these indicate which contact types to auto-add when manual_contact_mode = 0x01
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

void MyMesh::writeOKFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_OK;
  _serial->writeFrame(buf, 1);
}
void MyMesh::writeErrFrame(uint8_t err_code) {
  uint8_t buf[2];
  buf[0] = RESP_CODE_ERR;
  buf[1] = err_code;
  _serial->writeFrame(buf, 2);
}

void MyMesh::writeDisabledFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_DISABLED;
  _serial->writeFrame(buf, 1);
}

void MyMesh::writeContactRespFrame(uint8_t code, const ContactInfo &contact) {
  int i = 0;
  out_frame[i++] = code;
  memcpy(&out_frame[i], contact.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_frame[i++] = contact.type;
  out_frame[i++] = contact.flags;
  out_frame[i++] = contact.out_path_len;
  memcpy(&out_frame[i], contact.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  StrHelper::strzcpy((char *)&out_frame[i], contact.name, 32);
  i += 32;
  memcpy(&out_frame[i], &contact.last_advert_timestamp, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lat, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lon, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.lastmod, 4);
  i += 4;
  _serial->writeFrame(out_frame, i);
}

void MyMesh::updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len) {
  int i = 0;
  uint8_t code = frame[i++]; // eg. CMD_ADD_UPDATE_CONTACT
  memcpy(contact.id.pub_key, &frame[i], PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  contact.type = frame[i++];
  contact.flags = frame[i++];
  contact.out_path_len = frame[i++];
  memcpy(contact.out_path, &frame[i], MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  memcpy(contact.name, &frame[i], 32);
  // The frame comes from the app and nothing guarantees a NUL inside the 32
  // bytes; an unterminated name would later overrun strcpy/strlen consumers
  // (e.g. the AdvertPath name cache).
  contact.name[sizeof(contact.name) - 1] = '\0';
  i += 32;
  memcpy(&contact.last_advert_timestamp, &frame[i], 4);
  i += 4;
  if (len >= i + 8) { // optional fields
    memcpy(&contact.gps_lat, &frame[i], 4);
    i += 4;
    memcpy(&contact.gps_lon, &frame[i], 4);
    i += 4;
    if (len >= i + 4) {
      memcpy(&last_mod, &frame[i], 4);
    }
  }
}

bool MyMesh::Frame::isChannelMsg() const {
  return buf[0] == RESP_CODE_CHANNEL_MSG_RECV || buf[0] == RESP_CODE_CHANNEL_MSG_RECV_V3 ||
         buf[0] == RESP_CODE_CHANNEL_DATA_RECV;
}

void MyMesh::addToOfflineQueue(const uint8_t frame[], int len) {
  if (offline_queue_len >= OFFLINE_QUEUE_SIZE) {
    MESH_DEBUG_PRINTLN("WARN: offline_queue is full!");
    int pos = 0;
    while (pos < offline_queue_len) {
      if (offline_queue[pos].isChannelMsg()) {
        for (int i = pos; i < offline_queue_len - 1; i++) { // delete oldest channel msg from queue
          offline_queue[i] = offline_queue[i + 1];
        }
        MESH_DEBUG_PRINTLN("INFO: removed oldest channel message from queue.");
        offline_queue[offline_queue_len - 1].len = len;
        memcpy(offline_queue[offline_queue_len - 1].buf, frame, len);
        return;
      }
      pos++;
    }
    MESH_DEBUG_PRINTLN("INFO: no channel messages to remove from queue.");
  } else {
    offline_queue[offline_queue_len].len = len;
    memcpy(offline_queue[offline_queue_len].buf, frame, len);
    offline_queue_len++;
  }
}

int MyMesh::getFromOfflineQueue(uint8_t frame[]) {
  if (offline_queue_len > 0) {         // check offline queue
    size_t len = offline_queue[0].len; // take from top of queue
    memcpy(frame, offline_queue[0].buf, len);

    offline_queue_len--;
    for (int i = 0; i < offline_queue_len; i++) { // delete top item from queue
      offline_queue[i] = offline_queue[i + 1];
    }
    return len;
  }
  return 0; // queue is empty
}

float MyMesh::getAirtimeBudgetFactor() const {
  return _prefs.airtime_factor;
}

int MyMesh::getInterferenceThreshold() const {
  return _prefs.interference_threshold;
}
bool MyMesh::getCADEnabled() const {
  // RSSI-threshold interference detection relies on _noise_floor, which is only
  // kept fresh by continuous RX — stale during RX duty-cycle sleep. Auto-enable
  // hardware CAD (a fresh explicit scan) whenever power-save is actually active.
  // _prefs.cad_enabled is the manual override: Settings > Radio's advanced
  // options and the companion CLI's `cad` (MyMeshCli.h) set it.
  // rx_powersave is never actually applied to the radio while FEAT_RX_POWERSAVE
  // is 0 (see Features.h) -- don't let a stale persisted byte from before that
  // still auto-enable CAD here for a duty-cycle mode that isn't running.
#if FEAT_RX_POWERSAVE
  return _prefs.cad_enabled || (_prefs.rx_powersave && !_prefs.client_repeat);
#else
  return _prefs.cad_enabled;
#endif
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  uint32_t d = getRNG()->nextInt(0, 5*t + 1);
  // Yield filter (Tools > Repeater): scale the flood retransmit delay so a
  // mobile companion waits longer and lets better-sited fixed repeaters win the
  // flood first. Only forwarded floods reach here — own sends pass their own
  // delay to sendFlood() — so this never slows the companion's own traffic.
  return d * (1 + _prefs.repeat_delay_boost);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

uint8_t MyMesh::getExtraAckTransmitCount() const {
  return _prefs.multi_acks;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {
    int i = 0;
    out_frame[i++] = PUSH_CODE_LOG_RX_DATA;
    out_frame[i++] = (int8_t)(snr * 4);
    out_frame[i++] = (int8_t)(rssi);
    memcpy(&out_frame[i], raw, len);
    i += len;

    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::isAutoAddEnabled() const {
  return (_prefs.manual_add_contacts & 1) == 0;
}

bool MyMesh::shouldAutoAddContactType(uint8_t contact_type) const {
  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;
  }

  uint8_t type_bit = 0;
  switch (contact_type) {
    case ADV_TYPE_CHAT:
      type_bit = AUTO_ADD_CHAT;
      break;
    case ADV_TYPE_REPEATER:
      type_bit = AUTO_ADD_REPEATER;
      break;
    case ADV_TYPE_ROOM:
      type_bit = AUTO_ADD_ROOM_SERVER;
      break;
    case ADV_TYPE_SENSOR:
      type_bit = AUTO_ADD_SENSOR;
      break;
    default:
      return false;  // Unknown type, don't auto-add
  }

  return (_prefs.autoadd_config & type_bit) != 0;
}

bool MyMesh::shouldOverwriteWhenFull() const {
  return (_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

uint8_t MyMesh::getAutoAddMaxHops() const {
  return _prefs.autoadd_max_hops;
}

void MyMesh::onContactOverwrite(const uint8_t* pub_key) {
    _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE); // delete from storage
  if (_listener) _listener->onContactRemoved(pub_key); // same cleanup as an explicit CMD_REMOVE_CONTACT
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    memcpy(&out_frame[1], pub_key, PUB_KEY_SIZE);
    _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
  }
}

void MyMesh::onContactsFull() {
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACTS_FULL;
    _serial->writeFrame(out_frame, 1);
  }
}

void MyMesh::onDiscoveredAdvert(bool was_flood) {
  if (_listener) _listener->onAdvertHeard(was_flood);
}

// The position an advert carries, when it carries one: a contact's stored one
// may be older than what an SOS already gave. Not from a replay of an older one.
void MyMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp,
                          const uint8_t* app_data, size_t app_data_len) {
  const ContactInfo* known = lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE);
  const bool fresh = !known || timestamp > known->last_advert_timestamp;
  BaseChatMesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len);
  if (!fresh || !_listener) return;
  AdvertDataParser parser(app_data, app_data_len);
  if (parser.isValid() && parser.hasLatLon())
    _listener->onAdvertPosition(id.pub_key, parser.getName(), parser.getIntLat(), parser.getIntLon());
}

void MyMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (_serial->isConnected()) {
    if (is_new) {
      writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact);
    } else {
      out_frame[0] = PUSH_CODE_ADVERT;
      memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
      _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
    }
  }

  // add inbound-path to mem cache
  if (path && mesh::Packet::isValidPathLen(path_len)) {  // check path is valid
    AdvertPath* p = advert_paths;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {   // check if already in table, otherwise evict oldest
      if (memcmp(advert_paths[i].pubkey_prefix, contact.id.pub_key, sizeof(AdvertPath::pubkey_prefix)) == 0) {
        p = &advert_paths[i];   // found
        break;
      }
      if (advert_paths[i].recv_timestamp < oldest) {
        oldest = advert_paths[i].recv_timestamp;
        p = &advert_paths[i];
      }
    }

    memcpy(p->pubkey_prefix, contact.id.pub_key, sizeof(p->pubkey_prefix));
    strcpy(p->name, contact.name);
    p->recv_timestamp = getRTCClock()->getCurrentTime();
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }

  if (_listener) _listener->onDiscoveredContact(contact, is_new, path_len, path);

  if (!is_new) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); // only schedule lazy write for contacts that are in contacts[]
}

static int sort_by_recent(const void *a, const void *b) {
  return ((AdvertPath *) b)->recv_timestamp - ((AdvertPath *) a)->recv_timestamp;
}

int MyMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  if (max_num > ADVERT_PATH_TABLE_SIZE) max_num = ADVERT_PATH_TABLE_SIZE;
  qsort(advert_paths, ADVERT_PATH_TABLE_SIZE, sizeof(advert_paths[0]), sort_by_recent);

  for (int i = 0; i < max_num; i++) {
    dest[i] = advert_paths[i];
  }
  return max_num;
}

void MyMesh::onContactPathUpdated(const ContactInfo &contact) {
  if (_path_kept) return;   // a kept route: the path heard isn't taken (onContactPathRecv)
  out_frame[0] = PUSH_CODE_PATH_UPDATED;
  memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE); // NOTE: app may not be connected

  // Only the path changed: lost on a reset it costs one flood, so its write
  // waits; the same path heard again isn't written at all.
  if (!_path_same && !dirty_paths_expiry) dirty_paths_expiry = futureMillis(LAZY_PATHS_WRITE_DELAY);
}

ContactInfo*  MyMesh::processAck(const uint8_t *data) {
  // 0 marks an empty/cleared table slot, not a real ACK -- a bare memcmp below
  // would otherwise match the first free slot against an all-zero `data` (which
  // *is* remotely reachable, ack_crc comes off the air) and misattribute the
  // ACK to whatever contact that slot last belonged to.
  static const uint8_t zero_ack[4] = {0, 0, 0, 0};
  if (memcmp(data, zero_ack, 4) == 0) return checkConnectionsAck(data);
  // see if matches any in a table
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++) {
    if (expected_ack_table[i].ack == 0) continue;   // empty slot
    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient
      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
      memcpy(&out_frame[1], data, 4);
      uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;
      memcpy(&out_frame[5], &trip_time, 4);
      _serial->writeFrame(out_frame, 9);

      ContactInfo* contact = expected_ack_table[i].contact;
      // NOTE: the same ACK can be received multiple times!
      expected_ack_table[i].ack = 0;            // clear expected hash, now that we have received ACK
      expected_ack_table[i].contact = nullptr;  // and the stale pointer along with it
      return contact;
    }
  }
  return checkConnectionsAck(data);
}

void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, int extra_len, const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], from.id.pub_key, 6);
  i += 6; // just 6-byte prefix
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = txt_type;
  memcpy(&out_frame[i], &sender_timestamp, 4);
  i += 4;
  if (extra_len > 0) {
    memcpy(&out_frame[i], extra, extra_len);
    i += extra_len;
  }
  int tlen = strlen(text);
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = mesh::validUtf8PrefixLength(text, MAX_FRAME_SIZE - i); // don't split a multi-byte char
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

  if (_listener) {
    // Queue size first: the UI caps its room-unread count against it when the
    // message itself arrives (upstream sends it after; order is harmless there).
    _listener->onQueueSizeChanged(offline_queue_len);
    _listener->onMessageRecvEx(pkt, from, txt_type, sender_timestamp, extra, extra_len, text);
  }
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // APC: a channel/flood packet we originated, heard back from a repeater, is
  // positive feedback on the repeater link — sample its SNR. This is the only
  // confirmation a channel send gets (no ACK), and is what lets APC recover power
  // after it has trimmed too far for the repeaters to hear.
  if (apcActive() && _apc_flood_pending && packet->payload_len == _apc_flood_len) {
    uint8_t h[MAX_HASH_SIZE];
    packet->calculatePacketHash(h);
    if (memcmp(h, _apc_flood_hash, MAX_HASH_SIZE) == 0) {
      _apc_flood_pending = false;
      apcSampleSnr(packet->getSNR());
    }
  }
  // UI relayed-into-mesh marker: same heard-echo idea, runs regardless of APC.
  // Gated on _relay_active so the hash is only computed while a send is pending.
  if (_relay_active > 0) {
    uint8_t h[MAX_HASH_SIZE];
    bool hashed = false;
    for (int i = 0; i < RELAY_RING; i++) {
      RelaySlot& s = _relay[i];
      if (!s.pending || s.len != packet->payload_len) continue;
      if (!hashed) { packet->calculatePacketHash(h); hashed = true; }
      if (memcmp(h, s.hash, MAX_HASH_SIZE) == 0) {
        // Slot stays pending (freed only by the deadline sweep elsewhere) so a
        // DIFFERENT repeater's independent echo of this same send can still
        // match here too -- onChannelRelayed()/markChannelRelayed() append each
        // additionally heard repeater instead of just flipping a single flag.
        if (_listener) {
          uint8_t hash_size = packet->getPathHashSize();
          uint8_t hop_count = packet->getPathHashCount();
          // The repeater we just heard directly is always the LAST hop appended
          // (Mesh.cpp appends its own hash on every retransmit) -- that's the
          // one within our own earshot, regardless of how many further hops
          // this same packet may go on to take beyond it.
          const uint8_t* repeater_hash = hop_count > 0 ? &packet->path[(hop_count - 1) * hash_size] : nullptr;
          _listener->onChannelRelayed(s.seq, repeater_hash, repeater_hash ? hash_size : 0);
        }
        break;
      }
    }
  }
  // REVISIT: try to determine which Region (from transport_codes[1]) that Sender is indicating for replies/responses
  //    if unknown, fallback to finding Region from transport_codes[0], the 'scope' used by Sender
  return false;
}

// Loop guard for flood packets, ported from simple_repeater's LOOP_DETECT_MODERATE
// thresholds (max times this node's hash may already appear in the path, by hash size).
// A companion moves around, so it re-enters its own flood's path more easily than a
// fixed repeater — hardcoded rather than configurable since there's no CLI here.
// Indexed by getPathHashSize() = (path_len>>6)+1, so 1..4. Index 0 is unused
// (hash size is never 0); index 4 covers path_mode 3, which tryParsePacket
// currently rejects — kept in-bounds so this can't OOB-read if that guard is
// ever relaxed.
static const uint8_t REPEAT_LOOP_MAX[] = { 0, /*1-byte*/ 2, /*2-byte*/ 1, /*3-byte*/ 1, /*4-byte*/ 1 };
// Caps how many hops an ADVERT flood gets repeated, matching simple_repeater's default
// flood_max_advert — adverts are the most frequent flood traffic, so this is the one
// depth limit worth keeping even without the rest of simple_repeater's flood_max knobs.
static const uint8_t REPEAT_MAX_ADVERT_HOPS = 8;

bool MyMesh::isRepeatLooped(const mesh::Packet* packet) const {
  uint8_t hash_size = packet->getPathHashSize();
  if (hash_size >= sizeof(REPEAT_LOOP_MAX)) return true;  // unknown hash size: treat as looped, don't forward
  uint8_t hash_count = packet->getPathHashCount();
  uint8_t n = 0;
  const uint8_t* path = packet->path;
  while (hash_count > 0) {
    if (self_id.isHashMatch(path, hash_size)) n++;
    hash_count--;
    path += hash_size;
  }
  return n >= REPEAT_LOOP_MAX[hash_size];
}

bool MyMesh::allowPacketForward(const mesh::Packet* packet) {
  if (_prefs.client_repeat == 0) return false;
  // Forwarding filters (Tools > Repeater) — all default off, so a plain repeater
  // is unaffected. Flood-only by design: on a direct route this node is the named
  // next hop, so dropping there would kill delivery with no alternate path, while
  // dropping a flood copy just trims redundancy other nodes still carry.
  if (packet->isRouteFlood()) {
    if (_prefs.repeat_min_snr != NodePrefs::REPEAT_SNR_DISABLED
        && packet->getSNR() < (float)_prefs.repeat_min_snr) return false;
    if (_prefs.repeat_skip_adverts && packet->getPayloadType() == PAYLOAD_TYPE_ADVERT) return false;
    if (_prefs.repeat_max_hops > 0 && packet->getPathHashCount() >= _prefs.repeat_max_hops) return false;
    if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->getPathHashCount() >= REPEAT_MAX_ADVERT_HOPS) return false;
    if (_prefs.repeat_scope_only && repeat_scope_count > 0) {   // no-op while unconfigured
      if (!packet->hasTransportCodes()) return false;   // unscoped flood, we only want our own scope(s)
      bool matched = false;
      for (uint8_t i = 0; i < repeat_scope_count && !matched; i++) {
        matched = repeat_scopes[i].calcTransportCode(packet) == packet->transport_codes[0];
      }
      if (!matched) return false;
    }
    if (isRepeatLooped(packet)) return false;
  }
  return true;
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, _prefs.path_hash_mode + 1);
  }
}

void MyMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: dynamic send_scope, depending on recipient and current 'home' Region
  if (_oneshot_scope_on) {
    sendFloodScoped(_oneshot_scope, pkt, delay_millis);
  } else if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope = _scope_list.key(_scope_list.default_idx);
    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}
void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  if (apcActive()) apcTrackFloodSend(pkt);   // listen for a repeater echo to drive APC (channels have no ACK)
  trackRelaySend(pkt);                          // and for the UI "relayed" marker
  if (_oneshot_scope_on) {
    sendFloodScoped(_oneshot_scope, pkt, delay_millis);
  } else if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else if (!send_scope.isNull()) {
    // App-driven per-send override (CMD_SET_FLOOD_SCOPE_KEY) still wins over
    // this channel's own on-device pick, same precedence DMs already have.
    sendFloodScoped(send_scope, pkt, delay_millis);
  } else {
    // THIS channel's own scope (Messages > channel context menu > Scope:):
    // "Default" follows the list's default, "*" sends unscoped. A channel we
    // can't identify (findChannelIdx() == -1, e.g. a send whose secret isn't
    // in channels[]) takes the default too.
    int channel_idx = findChannelIdx(channel);
    uint8_t list_idx = channel_idx >= 0 ? channelScope((uint8_t)channel_idx) : _scope_list.default_idx;
    TransportKey scope = _scope_list.key(list_idx);
    sendFloodScoped(scope, pkt, delay_millis);
  }
}


// Someone's SOS (SosEngine::compose()), `body` without the name in front.
void MyMesh::noteSos(const uint8_t* pub_key, const char* name, const char* body, bool verified) {
  int32_t lat, lon;
  if (!_listener) return;
  if (geo::parseSos(body, lat, lon)) _listener->onSosPosition(pub_key, name, lat, lon, verified);
  else if (geo::isSos(body)) _listener->onSosNoPosition(name);
}

void MyMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);

  // Live position share: a verified DM, so key the track by the sender's pubkey.
  int32_t loc_lat, loc_lon;
  if (_listener && geo::parseLocShare(text, loc_lat, loc_lon)) {
    _listener->onSharedLocation(from.id.pub_key, from.name, loc_lat, loc_lon, sender_timestamp, true);
  } else {
    noteSos(from.id.pub_key, from.name, text, true);
  }

  // hop count of the received message. getPathHashCount() (low 6 bits of path_len)
  // is the number of repeaters traversed — the same value the mesh uses for flood
  // retransmit priority. 0 = heard directly. (Raw path_len is a size/count
  // bitfield for transport packets, so it must not be used directly.)
  uint8_t hops = pkt ? pkt->getPathHashCount() : 0;
  setBotInbound(pkt, &from);
  if (!tryBotCommand(from, text, hops))  // commands take priority; fall through to trigger reply
    tryBotReplyDM(from, text, hops);
}

void MyMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp, const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);
  // If the on-device Admin screen sent this command (not the BLE/USB app's CLI
  // terminal), also hand the reply straight to the UI -- queueMessage() above
  // never displays TXT_TYPE_CLI_DATA on-device (see should_display), since that
  // path also serves the app's terminal, which must keep working unaffected.
  if (_listener && ui_pending_admin_reply && memcmp(&ui_pending_admin_reply, from.id.pub_key, 4) == 0) {
    ui_pending_admin_reply = 0;
    _listener->onAdminReply(from.id.pub_key, text);
  }
}

// Oldest room post the bot still answers (see onSignedMessageRecv()).
#define BOT_ROOM_MAX_AGE_SECS  300
void MyMesh::onCLICommandRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                               const char *text, char* reply) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  if (from.isRemoteCLIAllowed()) {
    if (!handleCommand(text, sender_timestamp, reply)) {
      strcat(reply, "Unknown command");   // reply may have cmd prefix from 'text'
    }
  } else {
    queueMessage(from, TXT_TYPE_CLI_COMMAND, pkt, sender_timestamp, NULL, 0, text);
  }
}

void MyMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                                 const uint8_t *sender_prefix, const char *text) {
  markConnectionActive(from);
  // from.sync_since change needs to be persisted
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);

  // Live position share inside a room (mirrors the DM and channel paths). The
  // post's author is the signed sender_prefix, not the room server `from`, so
  // resolve that 4-byte prefix to a contact name and track by name. Unverified:
  // we only hold a 4-byte prefix here, not the full pubkey LiveTrack keys on.
  int32_t loc_lat, loc_lon;
  if (_listener && geo::parseLocShare(text, loc_lat, loc_lon)) {
    ContactInfo* sc = sender_prefix ? lookupContactByPubKey(sender_prefix, 4) : nullptr;
    const char* who = (sc && sc->name[0]) ? sc->name : from.name;
    _listener->onSharedLocation(nullptr, who, loc_lat, loc_lon, sender_timestamp, false);
  } else {
    ContactInfo* sc = sender_prefix ? lookupContactByPubKey(sender_prefix, 4) : nullptr;
    noteSos(nullptr, (sc && sc->name[0]) ? sc->name : from.name, text, false);
  }

  // Room-server auto-reply bot — only ever fires for the room server contact
  // itself (signed posts are how a room relays its members' messages back to
  // us); a defensive type check lives in tryBotReplyRoom/tryBotRoomCommand too.
  // After login the room replays posts we missed: answer only fresh ones, or
  // the bot would reply to (and run commands from) the whole backlog. A clock
  // behind the room's makes every post look new, which keeps today's behaviour.
  uint32_t now = getRTCClock()->getCurrentTime();
  bool replayed = now > sender_timestamp && now - sender_timestamp > BOT_ROOM_MAX_AGE_SECS;
  if (from.type == ADV_TYPE_ROOM && !replayed) {
    uint8_t hops = pkt ? pkt->getPathHashCount() : 0;
    setBotInbound(pkt, sender_prefix ? lookupContactByPubKey(sender_prefix, 4) : nullptr);
    if (!tryBotRoomCommand(from, sender_prefix, text, hops))  // commands take priority
      tryBotReplyRoom(from, sender_prefix, text, hops);
  }
}

void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
#ifdef PRIVATE_STATUS_REPORTER
  handleTrackerMessage(channel, pkt, text);
#endif
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  // findChannelIdx() returns -1 for an unknown secret (e.g. a packet that
  // routed to us through a stale hash collision, or a stored channel slot
  // whose secret was corrupted). Casting -1 to uint8_t would give 255, and
  // every downstream path (offline queue, UI hist, bot reply) would then
  // operate on a bogus channel index. Drop the message instead.
  int idx = findChannelIdx(channel);
  if (idx < 0) {
    MESH_DEBUG_PRINTLN("onChannelMessageRecv: unknown channel secret — dropping message");
    return;
  }
  uint8_t channel_idx = (uint8_t)idx;
#ifdef SOLO_GAMES_ENABLED
  if (_listener) {
    const char* body = strstr(text, ": ");
    if (body && (!strncmp(body + 2, "~G1:", 4) || !strncmp(body + 2, "~M1:", 4))) {
      char sender[40];
      size_t length = size_t(body - text);
      if (length >= sizeof(sender)) length = sizeof(sender) - 1;
      memcpy(sender, text, length);
      sender[length] = 0;
      _listener->onGameChannelText(channel_idx, body + 2, sender);
    }
  }
#endif
  out_frame[i++] = channel_idx;
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text);
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = mesh::validUtf8PrefixLength(text, MAX_FRAME_SIZE - i); // don't split a multi-byte char
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
  if (_listener) {
    ChannelDetails channel_details;
    if (!getChannel(channel_idx, channel_details)) {
      strcpy(channel_details.name, "Unknown");
    }
    _listener->onQueueSizeChanged(offline_queue_len);   // first -- see queueMessage()
    _listener->onChannelMessageRecvEx(pkt, channel_idx, channel_details, timestamp, text);
  }

  // Live position share on a channel. The sender's identity here is only the
  // unsigned "name: msg" prefix (no pubkey), so track it by name — best-effort
  // and unverified. parseLocShare requires an explicit [LOC] tag, so ordinary
  // chatter is ignored.
  int32_t loc_lat, loc_lon;
  if (_listener && geo::parseLocShare(text, loc_lat, loc_lon)) {
    char sender[32];
    const char* msg;
    botChannelSenderSplit(text, sender, sizeof(sender), &msg);
    _listener->onSharedLocation(nullptr, sender, loc_lat, loc_lon, timestamp, false);
  } else {
    char sender[32];
    const char* msg;
    botChannelSenderSplit(text, sender, sizeof(sender), &msg);
    noteSos(nullptr, sender, msg, false);
  }

#ifndef PRIVATE_STATUS_REPORTER
  // hop count for !hops (see onMessageRecv); not the wire path_len above.
  uint8_t ch_hops = pkt->getPathHashCount();
  if (botChSlotFor(channel_idx) >= 0) {   // a bot's channel: who's asking
    char sender[32];
    const char* msg;
    botChannelSenderSplit(text, sender, sizeof(sender), &msg);
    setBotInbound(pkt, botContactByName(sender));
  }
  if (!tryBotChannelCommand(channel_idx, text, ch_hops))  // commands take priority
    tryBotReplyChannel(channel_idx, text, ch_hops);
#endif
}

void MyMesh::onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                               const uint8_t *data, size_t data_len) {
  if (data_len > MAX_CHANNEL_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("onChannelDataRecv: dropping payload_len=%d exceeds frame limit=%d",
                       (uint32_t)data_len, (uint32_t)MAX_CHANNEL_DATA_LENGTH);
    return;
  }

  int i = 0;
  out_frame[i++] = RESP_CODE_CHANNEL_DATA_RECV;
  out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
  out_frame[i++] = 0; // reserved1
  out_frame[i++] = 0; // reserved2

  int didx = findChannelIdx(channel);
  if (didx < 0) {
    MESH_DEBUG_PRINTLN("onChannelDataRecv: unknown channel secret — dropping packet");
    return;
  }
  uint8_t channel_idx = (uint8_t)didx;
  out_frame[i++] = channel_idx;
  out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = (uint8_t)(data_type & 0xFF);
  out_frame[i++] = (uint8_t)(data_type >> 8);
  out_frame[i++] = (uint8_t)data_len;

  int copy_len = (int)data_len;
  if (copy_len > 0) {
    memcpy(&out_frame[i], data, copy_len);
    i += copy_len;
  }
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
  if (_listener) {
    _listener->onChannelDataRecv(pkt, channel, data_type, data, data_len);
  }
}

uint8_t MyMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                 uint8_t len, uint8_t *reply) {
  if (data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t permissions = 0;
    uint8_t cp = contact.flags >> 1; // LSB used as 'favourite' bit (so only use upper bits)

    if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL) {
      permissions = TELEM_PERM_BASE;
    } else if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS) {
      permissions = cp & TELEM_PERM_BASE;
    }

    if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_LOCATION;
    } else if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_LOCATION;
    }

    if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_ENVIRONMENT;
    } else if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_ENVIRONMENT;
    }

    uint8_t perm_mask = ~(data[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions
    permissions &= perm_mask;
#ifdef PRIVATE_STATUS_REPORTER
    permissions &= ~TELEM_PERM_LOCATION;
#endif


    if (permissions & TELEM_PERM_BASE) { // only respond if base permission bit is set
      telemetry.reset();
      telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      // query other sensors -- target specific
      sensors.querySensors(permissions, telemetry);

      float temperature = board.getMCUTemperature();
      if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
        telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
      }

      memcpy(reply, &sender_timestamp,
             4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

      uint8_t tlen = telemetry.getSize();
      memcpy(&reply[4], telemetry.getBuffer(), tlen);
      return 4 + tlen;
    }
  } else if (_listener) {
    return _listener->onUnhandledRequest(contact, sender_timestamp, data, len, reply);
  }
  return 0; // unknown
}

void MyMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
  uint32_t tag;
  memcpy(&tag, data, 4);

  if (pending_login && memcmp(&pending_login, contact.id.pub_key, 4) == 0) { // check for login response
    // yes, is response to pending sendLogin()
    pending_login = 0;

    int i = 0;
    bool login_ok = false;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = 0; // legacy: is_admin = false
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6;                                     // pub_key_prefix
      login_ok = true;
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = data[6]; // permissions (eg. is_admin)
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
      memcpy(&out_frame[i], &tag, 4);
      i += 4; // NEW: include server timestamp
      out_frame[i++] = data[7]; // NEW (v7): ACL permissions
      out_frame[i++] = data[12]; // FIRMWARE_VER_LEVEL
      login_ok = true;
    } else {
      out_frame[i++] = PUSH_CODE_LOGIN_FAIL;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
    }
    // Persist app/USB-entered room passwords too, so the device can later post
    // to that room standalone (after reboot, no phone) without re-prompting --
    // same store the on-device login path uses. Rooms only; a failed login
    // forgets any stale saved password, mirroring onRoomLoginResult().
    if (contact.type == ADV_TYPE_ROOM) {
      if (login_ok) saveRoomPassword(contact.id.pub_key, pending_login_pw);
      else          forgetRoomPassword(contact.id.pub_key);
    }
    _serial->writeFrame(out_frame, i);
  } else if (ui_pending_login && memcmp(&ui_pending_login, contact.id.pub_key, 4) == 0) { // check for on-device UI login response
    ui_pending_login = 0;

    bool success;
    uint8_t permissions = 0;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      success = true;
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      success = true;
      permissions = data[7]; // ACL permissions
    } else {
      success = false;
    }
    if (_listener) _listener->onRoomLoginResult(contact.id.pub_key, success, permissions);
  } else if (len > 4 && // check for status response
             pending_status &&
             memcmp(&pending_status, contact.id.pub_key, 4) == 0 // legacy matching scheme
                                                                 // FUTURE: tag == pending_status
  ) {
    pending_status = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_telemetry) {  // check for matching response tag
    pending_telemetry = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_req) {  // check for matching response tag
    pending_req = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], &tag, 4);   // app needs to match this to RESP_CODE_SENT.tag
    i += 4;
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else {
    bool handled;
    if (_listener && len > 4) {
      handled = _listener->onUnhandledResponse(contact, tag, &data[4], len - 4);
    } else {
      handled = false;
    }
    if (!handled) {   // let the app know about unknown/unhandled responses (eg. push telemetry packets)
      int i = 0;
      out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], data, len);
      i += len;
      _serial->writeFrame(out_frame, i);
    }
  }
}

#define ROOM_PW_FILE "/room_pw"
#define ROOM_PW_TMP  "/room_pw.tmp"
#define MAX_SAVED_ROOM_PASSWORDS 16

namespace {
  struct RoomPwRec {
    uint8_t key[4];   // pub-key prefix
    char pw[16];      // up to 15 chars + NUL
  };
}

// 'Path Discovery' is just a special case of flood + Telemetry req: the
// reply comes back as a path return carrying both ways (onContactPathRecv()).
int MyMesh::sendDiscoveryReq(ContactInfo& recipient, uint32_t& tag, uint32_t& est_timeout) {
  uint8_t req_data[9];
  req_data[0] = REQ_TYPE_GET_TELEMETRY_DATA;
  req_data[1] = ~(TELEM_PERM_BASE);  // NEW: inverse permissions mask (ie. we only want BASE telemetry)
  memset(&req_data[2], 0, 3);  // reserved
  getRNG()->random(&req_data[5], 4);   // random blob to help make packet-hash unique
  auto save = recipient.out_path_len;    // temporarily force sendRequest() to flood
  recipient.out_path_len = OUT_PATH_UNKNOWN;
  int result = sendRequest(recipient, req_data, sizeof(req_data), tag, est_timeout);
  recipient.out_path_len = save;
  return result;
}

uint32_t MyMesh::sendPathDiscovery(const uint8_t* pub_key, DiscoverCallback cb, uint32_t* est_timeout) {
  ContactInfo* recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!recipient) return 0;
  uint32_t tag, est;
  if (sendDiscoveryReq(*recipient, tag, est) == MSG_SEND_FAILED) return 0;
  _ui_discovery = tag;
  _ui_disc_sent[_ui_disc_next] = tag;
  _ui_disc_next = (_ui_disc_next + 1) % UI_DISC_KEEP;
  _discover_cb = cb;
  if (est_timeout) *est_timeout = est;
  return tag;
}

bool MyMesh::onContactPathRecv(ContactInfo& contact, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 4) {
    uint32_t tag;
    memcpy(&tag, extra, 4);

    // This device's own (sendPathDiscovery()): to the editor while it waits
    // for it; late (timed out, stopped, an earlier try's) only dropped.
    for (int k = 0; k < UI_DISC_KEEP; k++) {
      if (!tag || _ui_disc_sent[k] != tag) continue;
      _ui_disc_sent[k] = 0;
      if (tag == _ui_discovery) {
        _ui_discovery = 0;
        if (_discover_cb && mesh::Packet::isValidPathLen(in_path_len) && mesh::Packet::isValidPathLen(out_path_len))
          _discover_cb(contact.id.pub_key, out_path, out_path_len, in_path, in_path_len);
      }
      return false;  // DON'T send reciprocal path, nor take it: the user picks
    }

    if (tag == pending_discovery) {  // check for matching response tag)
      pending_discovery = 0;

      if (!mesh::Packet::isValidPathLen(in_path_len) || !mesh::Packet::isValidPathLen(out_path_len)) {
        MESH_DEBUG_PRINTLN("onContactPathRecv, invalid path sizes: %d, %d", in_path_len, out_path_len);
      } else {
        int i = 0;
        out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
        out_frame[i++] = 0; // reserved
        memcpy(&out_frame[i], contact.id.pub_key, 6);
        i += 6; // pub_key_prefix
        out_frame[i++] = out_path_len;
        i += mesh::Packet::writePath(&out_frame[i], out_path, out_path_len);
        out_frame[i++] = in_path_len;
        i += mesh::Packet::writePath(&out_frame[i], in_path, in_path_len);
        // NOTE: telemetry data in 'extra' is discarded at present

        _serial->writeFrame(out_frame, i);
      }
      return false;  // DON'T send reciprocal path!
    }
  }
  // let base class handle received path and data
  int path_bytes = (out_path_len & 63) * ((out_path_len >> 6) + 1);
  _path_same = contact.out_path_len == out_path_len && path_bytes <= MAX_PATH_SIZE
            && memcmp(contact.out_path, out_path, path_bytes) == 0;
  // A route kept by hand stays: the base class takes the path and handles the
  // ACK / response riding on it, then the held route goes back -- with its
  // lastmod, and neither written nor announced to the app (_path_kept).
  const bool pinned = (contact.flags & CONTACT_FLAG_ROUTE_PINNED) != 0 && contact.out_path_len != OUT_PATH_UNKNOWN;
  uint8_t held_len = contact.out_path_len;
  uint32_t held_mod = contact.lastmod;
  uint8_t held[MAX_PATH_SIZE];
  if (pinned) {
    memcpy(held, contact.out_path, sizeof(held));
    _path_same = _path_kept = true;
  }
  bool r = BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
  _path_same = _path_kept = false;
  if (pinned) {
    contact.out_path_len = held_len;
    contact.lastmod = held_mod;
    memcpy(contact.out_path, held, sizeof(held));
  }
  // A flood DM's ACK comes back folded into the return path, which the base
  // class matches without going through onAckRecv() -- so the UI's delivery
  // marker never heard of it and kept resending. Tell it here too; it only
  // acts on a crc matching one of its own pending DMs.
  if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4 && _listener) {
    uint32_t ack_crc;
    memcpy(&ack_crc, extra, 4);
    _listener->onACKRecv(ack_crc);
  }
  return r;
}

#define CTL_TYPE_NODE_DISCOVER_REQ  0x80
#define CTL_TYPE_NODE_DISCOVER_RESP 0x90

void MyMesh::onControlDataRecv(mesh::Packet *packet) {
#ifdef PRIVATE_STATUS_REPORTER
  handleNearbyResponse(packet);
#endif

  // If we have an active standalone discover, check if this is a matching response.
  // Tag matching provides isolation — no isBLEConnected check needed.
  if ((packet->payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP &&
      packet->payload_len >= 6 + PUB_KEY_SIZE &&
      _pending_node_discover_tag != 0 &&
      !millisHasNowPassed(_pending_node_discover_until)) {
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag == _pending_node_discover_tag) {
      uint8_t node_type = packet->payload[0] & 0x0F;
      const uint8_t* pub_key = &packet->payload[6];
      if (isDupDiscoverResp(tag, pub_key)) return;  // second copy of the same response
      ContactInfo* known = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
      if (known) {
        known->lastmod = getRTCClock()->getCurrentTime();
      }
      if (_discover_count < DISCOVER_RESULTS_MAX) {
        DiscoverResult& r = _discover_results[_discover_count++];
        if (known) {
          strncpy(r.name, known->name, sizeof(r.name) - 1);
          r.name[sizeof(r.name) - 1] = '\0';
          r.is_known = true;
        } else {
          r.name[0] = '\0';
          r.is_known = false;
        }
        r.type = node_type;
        r.rssi = (int8_t)_radio->getLastRSSI();
        r.snr_x4 = (int8_t)(_radio->getLastSNR() * 4);
        r.remote_snr_x4 = (int8_t)packet->payload[1];
        memcpy(r.pub_key, pub_key, PUB_KEY_SIZE);
        r.timestamp = getRTCClock()->getCurrentTime();
      }
      if (_listener) _listener->onAdvertHeard(packet->isRouteFlood());
      return;  // our discover — don't forward to BLE app
    }
  }

  // App-driven discover: drop duplicate copies of the same response before
  // forwarding, so the app doesn't list one responder twice. (Our own
  // standalone discover already returned above on a tag match.)
  if ((packet->payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP &&
      packet->payload_len >= 6 + PUB_KEY_SIZE) {
    uint32_t fwd_tag;
    memcpy(&fwd_tag, &packet->payload[2], 4);
    if (isDupDiscoverResp(fwd_tag, &packet->payload[6])) return;
  }

  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_CONTROL_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = packet->path_len;
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), data received while app offline");
  }

  if (_listener) _listener->onControlDataRecv(packet);
}

void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_RAW_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = 0xFF; // reserved (possibly path_len in future)
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), data received while app offline");
  }
  if (_listener) {
    _listener->onRawDataRecv(packet);
  }
}

void MyMesh::onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  
  // Check if this is a response to our local ping
  for (int i = 0; i < PING_RESULT_MAX; i++) {
    if (_ping_results[i].tag == tag && _ping_results[i].auth_code == auth_code && !_ping_results[i].received) {
      PingResult& result = _ping_results[i];
      
      // Extract SNR values
      // path_snrs contains signed SNR values in dB×4 for each hop (in order).
      // The last value is the SNR at the destination hearing our request (snr_out).
      // The final SNR from packet is the SNR at us hearing the response (snr_back).
      uint8_t snr_count = path_len >> path_sz;
      
      if (snr_count > 0) {
        // Keep the encoded quarter-dB value; the UI converts it back to dB.
        result.snr_out_x4 = (int16_t)(int8_t)path_snrs[0];
      } else {
        result.snr_out_x4 = 0;
      }
      
      // SNR back is from the final SNR in the packet (SNR at us receiving response).
      result.snr_back_x4 = (int16_t)(packet->getSNR() * 4);
      
      // Calculate RTT
      unsigned long now = millis();
      if (now >= result.sent_ms) {
        result.rtt_ms = now - result.sent_ms;
      } else {
        result.rtt_ms = 0xFFFFFFFF; // Overflow
      }
      
      uint8_t n = snr_count > TRACE_SNR_MAX ? TRACE_SNR_MAX : snr_count;
      result.n_snr = n;
      memcpy(result.snrs_x4, path_snrs, n);

      result.received = true;
      
      // Notify callback if set
      if (_ping_callback) {
        _ping_callback(tag, result.snr_out_x4, result.snr_back_x4, result.rtt_ms);
      }
      // The UI copies the result out of the callback, so reuse the slot immediately.
      memset(&result, 0, sizeof(result));
      break;
    }
  }

  // Forward to serial app regardless (for compatibility)
  if (12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onTraceRecv(), path_len is too long: %d", (uint32_t)path_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_TRACE_DATA;
  out_frame[i++] = 0; // reserved
  out_frame[i++] = path_len;
  out_frame[i++] = flags;
  memcpy(&out_frame[i], &tag, 4);
  i += 4;
  memcpy(&out_frame[i], &auth_code, 4);
  i += 4;
  memcpy(&out_frame[i], path_hashes, path_len);
  i += path_len;

  memcpy(&out_frame[i], path_snrs, path_len >> path_sz);
  i += path_len >> path_sz;
  out_frame[i++] = (int8_t)(packet->getSNR() * 4); // extra/final SNR (to this node)

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onTraceRecv(), data received while app offline");
  }
  if (_listener) {
    _listener->onTraceRecv(packet, tag, auth_code, flags, path_snrs, path_hashes, path_len);
  }
}

uint32_t MyMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t MyMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t path_hash_count = path_len & 63;
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
          (path_hash_count + 1));
}

void MyMesh::onSendTimeout() {
  if (apcActive()) apcOnFailure();
}

void MyMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
  // onAckRecv also fires for ACKs we merely route or overhear (see Mesh.cpp), whose
  // SNR belongs to an unrelated link — only feed APC for ACKs to our own sends.
  // Capture the match before the base handler's processAck() clears the entry.
  bool mine = isAckPending(ack_crc);
  BaseChatMesh::onAckRecv(packet, ack_crc);
  if (mine && apcActive()) apcSampleSnr(radio_driver.getLastSNR());
  // Drive the DM delivery-status marker. Note isAckPending() only covers
  // app/serial-initiated sends (expected_ack_table); a DM composed on the
  // device UI registers in BaseChatMesh's own ack table instead, so gating on
  // `mine` here would leave every on-device DM stuck at ✗. The UI matches the
  // crc against its own pending tag, so an unrelated/overheard ACK is ignored.
  if (_listener) _listener->onACKRecv(ack_crc);
}

MyMesh::MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store)
    // Sized to match simple_repeater's pool (32), not the old client-only 16: with the
    // on-device Repeater toggle, queued retransmits (adverts/channel flood from
    // neighbours) can now hold packet-pool slots for their retransmit delay window. A
    // pool too small for that starves Dispatcher::checkRecv()'s allocNew(), which then
    // silently drops every incoming packet — DMs and channels included — until a slot
    // frees up.
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(32), tables),
      _serial(NULL), telemetry(MAX_PACKET_PAYLOAD - 4), _store(&store), _listener(NULL), _iter(0) {
  _iter_started = false;
  _cli_rescue = false;
  cli_command[0] = 0;
  for (int i = 0; i < RELAY_RING; i++) _relay[i].pending = false;
  _relay_head = 0;
  _relay_active = 0;
  _relay_seq = 0;
  _last_relay_seq = 0;
  offline_queue_len = 0;
  app_target_ver = 0;
  for (unsigned long& t : _bot_last_ch_reply_ms) t = 0;
  _bot_last_room_reply_ms = 0;
  memset(_bot_dm_log, 0, sizeof(_bot_dm_log));
  _bot_reply_count = 0;
  _next_auto_advert_ms = 0;
  _loc_fix.active = false;
  _locfix_requested = false;
  _locfix_requested_timeout_ms = LOCFIX_TIMEOUT_MS;
  _bot_gps_action_pending = false;
  _bot_buzz_action_secs = 0;
  _bot_advert_action_pending = false;
  for (int i = 0; i < 4; i++) _bot_gpio_action[i] = -1;
  clearPendingReqs();
  next_ack_idx = 0;
  sign_data = NULL;
  sign_data_cap = 0;
  dirty_contacts_expiry = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  memset(send_scope.key, 0, sizeof(send_scope.key));
  _discover_count = 0;
  _pending_node_discover_tag = 0;
  _pending_node_discover_until = 0;
  memset(_disc_seen, 0, sizeof(_disc_seen));
  _disc_seen_head = 0;

  // Ping state
  _ping_callback = NULL;
  _ping_callback_arg = NULL;
  memset(_ping_results, 0, sizeof(_ping_results));

  send_unscoped = false;
  repeat_scope_count = 0;

  // defaults
  _prefs.clearFields();
  memset(_prefs.ch_scope_idx, NodePrefs::CH_SCOPE_DEFAULT, sizeof(_prefs.ch_scope_idx));
  _prefs.airtime_factor = 1.0;
#ifdef ENABLE_WIFI_INTERFACE
  _prefs.wifi_enabled = 1;   // on, so a WiFi-only build works; it doesn't start without an SSID
#endif
  _prefs.tx_delay_factor = 0.5f;
  _prefs.direct_tx_delay_factor = 0.2f;
  strcpy(_prefs.node_name, "NONAME");
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  // The repeater's own radio profile, in the companion's band.
  seedDefaultRepeaterProfile(_prefs);
  _prefs.gps_enabled = 0;       // GPS disabled by default
  _prefs.gps_interval = 0;      // No automatic GPS updates by default
  _prefs.radio_fem_rxgain = 1;
#ifdef RADIO_FEM_TXGAIN
  _prefs.radio_fem_txgain = RADIO_FEM_TXGAIN;   // board-specific default for PA
#else
  _prefs.radio_fem_txgain = 0;
#endif
#ifdef DISPLAY_ROTATION
  _prefs.display_rotation = DISPLAY_ROTATION;
#endif
  _prefs.display_brightness = 2; // medium brightness by default
  _prefs.buzzer_volume = 4;      // max volume by default
  _prefs.quiet_from = 22;        // quiet hours (off by default) 22:00-07:00
  _prefs.quiet_to = 7;
  _prefs.ringtone_bpm_idx = 2;   // 120 bpm default
  _prefs.ringtone_len = 0;       // no custom ringtone by default
  _prefs.ringtone2_bpm_idx = 2;  // 120 bpm default
  _prefs.notif_melody_ad = 0;    // built-in advert sound by default
  _prefs.advert_sound_scope = ADVERT_SOUND_SCOPE_ALL;  // sound every advert by default
  _prefs.home_pages_mask = 0;  // all home pages visible by default (0 = all, see NodePrefs.h)
  _prefs.bot_enabled = 0;
  _prefs.bot_channel_enabled = 0;
  _prefs.bot_channel_idx = 0;
  _prefs.bot_trigger[0] = '\0';
  _prefs.bot_reply_dm[0] = '\0';
  _prefs.bot_reply_ch[0] = '\0';
  _prefs.bot_trigger_ch[0] = '\0';
  _prefs.bot_ch2_enabled = 0;
  _prefs.bot_ch2_idx = 0;
  _prefs.bot_reply_ch2[0] = '\0';
  _prefs.bot_trigger_ch2[0] = '\0';
  _prefs.bot_commands_enabled = 0;
  _prefs.bot_quiet_start = 0;
  _prefs.bot_quiet_end = 0;       // start==end → quiet hours disabled
  _prefs.bot_actions_scope = 0;   // actions by everyone
  _prefs.sos_countdown_idx = 3;   // 30 s: time to stop one started in a pocket
  _prefs.sos_advert = NodePrefs::SOS_ADVERT_ZERO_HOP;
  _prefs.sos_repeat_idx = 3;      // every 5 minutes
  strcpy(_prefs.sos_ack, "ACK");
  _prefs.cov_home_r = 2;   // 500 m
  _prefs.cov_auto = 1;
  strcpy(_prefs.sos_msg, "{loc} {alt} {time}");
  _prefs.dm_show_all = 1;        // show all contacts by default
  _prefs.fav_sort = 1;           // favourites first in lists
  _prefs.msg_wake = 1;           // a new message turns the screen on
  _prefs.dm_resend_count = 2;    // auto-resend on-device DMs twice by default
  memset(_prefs.dm_notif, 0, sizeof(_prefs.dm_notif));
  _prefs.auto_off_secs = 15;    // 15 seconds auto-off by default
  _prefs.clock_hide_seconds = Features::CLOCK_HIDE_SECONDS_DEFAULT ? 1 : 0;
  _prefs.eink_full_refresh_every = 2;   // e-ink: a full refresh every 10 partials, against ghosting
  _prefs.tz_offset_hours = 0;  // UTC by default
  _prefs.low_batt_mv = 3400;  // auto-shutdown at 3.4V by default
  _prefs.batt_display_mode = 0; // icon by default
  //_prefs.rx_delay_base = 10.0f;  enable once new algo fixed
  _prefs.client_repeat = 0;
  // 0 dB is a real threshold, so "off" is its own sentinel.
  _prefs.repeat_min_snr = NodePrefs::REPEAT_SNR_DISABLED;
#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default
#endif
#endif
}

void MyMesh::begin() {
  BaseChatMesh::begin();

  if (!_store->loadMainIdentity(self_id)) {
    self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      self_id = radio_new_identity();
      count++;
    }
    _store->saveMainIdentity(self_id);
  }

// if name is provided as a build flag, use that as default node name instead
#ifdef ADVERT_NAME
  strcpy(_prefs.node_name, ADVERT_NAME);
#else
  // use hex of first 4 bytes of identity public key as default node name
  char pub_key_hex[10];
  mesh::Utils::toHex(pub_key_hex, self_id.pub_key, 4);
  strcpy(_prefs.node_name, pub_key_hex);
#endif

  // if build provides default-scope, init with that
#ifdef DEFAULT_FLOOD_SCOPE_NAME
  strcpy(_prefs.default_scope_name, DEFAULT_FLOOD_SCOPE_NAME);
  {
    TransportKeyStore temp;
    TransportKey key;
    temp.getAutoKeyFor(0, "#" DEFAULT_FLOOD_SCOPE_NAME, key);
    memcpy(_prefs.default_scope_key, key.key, sizeof(key.key));
  }
#endif

  // load persisted prefs
  _store->loadPrefs(_prefs, sensors.node_lat, sensors.node_lon);
#ifdef PRIVATE_STATUS_REPORTER
  loadTrackerPrefs();
  _prefs.advert_loc_policy = ADVERT_LOC_NONE;
  _prefs.telemetry_mode_loc = TELEM_MODE_DENY;
  sensors.position_valid = tracker_prefs.status_position_known || sensors.node_lat != 0 || sensors.node_lon != 0;
#endif

  // True only on the first boot after upgrading a device that had the old
  // single Scope field set -- acted on once the channels are loaded, below.
  _store->loadScopeList(_scope_list, _prefs);
  rebuildRepeatScopes();

  // sanitise bad pref values. NaN/inf must be reset BEFORE constrain(): constrain
  // is a min/max macro and NaN compares false against both bounds, so it would
  // pass a NaN straight through to setParams() and hang the radio (a corrupted or
  // layout-shifted prefs file easily decodes a float field as NaN/inf).
  if (isnan(_prefs.freq) || isinf(_prefs.freq)) _prefs.freq = LORA_FREQ;
  if (isnan(_prefs.bw)   || isinf(_prefs.bw))   _prefs.bw   = LORA_BW;
  if (isnan(_prefs.airtime_factor) || isinf(_prefs.airtime_factor)) _prefs.airtime_factor = 0;
  if (isnan(_prefs.rx_delay_base)  || isinf(_prefs.rx_delay_base))  _prefs.rx_delay_base  = 0;
  _prefs.rx_delay_base = constrain(_prefs.rx_delay_base, 0, 20.0f);
  _prefs.tx_delay_factor = constrain(_prefs.tx_delay_factor, 0, 2.0f);
  _prefs.direct_tx_delay_factor = constrain(_prefs.direct_tx_delay_factor, 0, 2.0f);
  _prefs.airtime_factor = constrain(_prefs.airtime_factor, 0, 9.0f);
  _prefs.freq = constrain(_prefs.freq, 150.0f, 2500.0f);
  _prefs.bw = constrain(_prefs.bw, 7.8f, 500.0f);
  _prefs.sf = constrain(_prefs.sf, 5, 12);
  _prefs.cr = constrain(_prefs.cr, 5, 8);
  _prefs.tx_power_dbm = constrain(_prefs.tx_power_dbm, -9, MAX_LORA_TX_POWER);
  _prefs.gps_enabled = constrain(_prefs.gps_enabled, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_interval = constrain(_prefs.gps_interval, 0, 86400);  // Max 24 hours
  _prefs.gps_time_off = _prefs.gps_time_off ? 1 : 0;
  LocationProvider::timeFromGps() = !_prefs.gps_time_off;

  // The session PIN (random when a display can show it) is resolved by
  // main.cpp, which knows whether there is one -- see resolveBLEPin().
  _active_ble_pin = _prefs.ble_pin;

  resetContacts();
  _store->loadContacts(this);
  bootstrapRTCfromContacts();
  // Only seed the default Public channel on a genuinely fresh device (no
  // channels file at all yet) -- a deleted channel is simply absent from
  // /channels3, not written back as an empty record (see saveChannels()), so
  // seeding unconditionally here would silently resurrect Public every boot
  // even after the user explicitly deleted it.
  if (!_store->loadChannels(this)) {
    addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  }

  applyRepeaterRadio();   // companion params, or the repeater profile if relaying with one set
  applyApc();                                         // sets TX power to the ceiling and arms APC if enabled
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
#if FEAT_RX_POWERSAVE
  radio_driver.setPowerSaving(_prefs.client_repeat ? 0 : _prefs.rx_powersave);   // duty-cycle RX off while repeating (must hear all traffic)
#else
  radio_driver.setPowerSaving(0);   // see Features.h FEAT_RX_POWERSAVE -- ignore any stale persisted rx_powersave byte
#endif
  board.attachDynamicPrefs(_prefs.getCustom());   // the board's own settings (FEM gains): read now, set by its CLI
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
}

void MyMesh::applyRepeaterRadio() {
  if (_prefs.client_repeat && _prefs.repeater_use_profile && repeaterProfileValid())
    radio_driver.setParams(_prefs.repeater_freq, _prefs.repeater_bw, _prefs.repeater_sf, _prefs.repeater_cr);
  else
    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
}

const char *MyMesh::getNodeName() {
  return _prefs.node_name;
}
NodePrefs *MyMesh::getNodePrefs() {
  return &_prefs;
}
uint32_t MyMesh::getBLEPin() {
  return _active_ble_pin;
}
void MyMesh::setBLEPin(uint32_t active_pin) {
  _active_ble_pin = active_pin;
}

struct FreqRange {
  uint32_t lower_freq, upper_freq;
};

static FreqRange repeat_freq_ranges[] = {
  #ifdef ALLOWED_REPEAT_FREQ_RANGE
  ALLOWED_REPEAT_FREQ_RANGE
  #else
  { 433000, 433000 },
  { 869495, 869495 },
  { 918000, 918000 }
  #endif
};

bool MyMesh::isValidClientRepeatFreq(uint32_t f) const {
  for (int i = 0; i < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]); i++) {
    auto r = &repeat_freq_ranges[i];
    if (f >= r->lower_freq && f <= r->upper_freq) return true;
  }
  return false;
}

void MyMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();   // main.cpp then turns Bluetooth off if the user left it off (ble_off)
}

static bool isAllZero(const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) if (buf[i]) return false;
  return true;
}

// Shared by the BLE/USB CMD_SET_CHANNEL handler below and the on-device
// Channels add/edit/delete UI (ChannelsView) -- one place computing "was this
// a delete" so the two callers can't drift on the cleanup step.
bool MyMesh::setChannelLocal(uint8_t idx, const ChannelDetails& ch) {
  if (!setChannel(idx, ch)) return false;
  saveChannels();
  // An all-zero secret is this codebase's "empty slot" sentinel (same check
  // loadChannels()/saveChannels() use) -- drop anything that referenced it by
  // index, the same way onContactRemoved() does for contacts.
  if (_listener && isAllZero(ch.channel.secret, sizeof(ch.channel.secret)))
    _listener->onChannelRemoved(idx);
  return true;
}

#ifdef SOLO_GAMES_ENABLED
bool MyMesh::canSendGameMessage() const {
#ifdef PRIVATE_STATUS_REPORTER
  // Keep paced tracker replies ahead of games, including between reply sends.
  if (tracker_reply_count) return false;
#endif
  return _mgr->getOutboundTotal() == 0;
}

bool MyMesh::getGameChannel(uint8_t slot, ChannelDetails& channel, uint8_t digest[16]) {
#if defined(SOLO_GAMES_ENABLED) && defined(PRIVATE_STATUS_REPORTER)
  // Exactly the same privacy classification as the existing tracking channel
  // picker. No tracking binding, origin record or extra confirmation required.
  if (!getChannel(slot, channel) || privateStatusChannel(channel)) return false;
  uint8_t hash[32];
  mesh::Utils::sha256(hash, sizeof(hash), channel.channel.secret, sizeof(channel.channel.secret));
  memcpy(digest, hash, 16);
  return true;
#else
  return false;
#endif
}

#ifdef SOLO_GAME_VEYLITH
// Separate from radio identity, channels, Solo preferences and GPS trails.
#ifdef ZYGIS_GAME_TEST_API
static const char* VEYLITH_FILE="/veytest1";
static const char* VEYLITH_TMP="/veytest.tmp";
#else
static const char* VEYLITH_FILE="/veylith1";
static const char* VEYLITH_TMP="/veylith.tmp";
#endif
int MyMesh::loadVeylith(uint8_t* bytes,size_t size) {
  File file=_store->openRead(VEYLITH_FILE);
  if(!file)return -1;
  if(file.size()!=size){file.close();return -2;}
  bool ok=file.read(bytes,size)==int(size);file.close();return ok?int(size):-2;
}
bool MyMesh::saveVeylith(const uint8_t* bytes,size_t size) {
  if(size!=437)return false;
  File file=_store->openWrite(VEYLITH_TMP);if(!file)return false;
  bool ok=file.write(bytes,size)==size;file.close();
  if(!ok){_store->removeFile(VEYLITH_TMP);return false;}
  return _store->commitFile(VEYLITH_TMP,VEYLITH_FILE);
}
bool MyMesh::backupVeylith() {
  File old=_store->openRead(VEYLITH_FILE);if(!old)return false;
  File copy=_store->openWrite("/veylith.bad.tmp");if(!copy){old.close();return false;}
  uint8_t buffer[64];bool ok=true;
  while(old.available()){int n=old.read(buffer,sizeof(buffer));if(n<=0 || copy.write(buffer,n)!=size_t(n)){ok=false;break;}}
  old.close();copy.close();
  if(!ok){_store->removeFile("/veylith.bad.tmp");return false;}
  return _store->commitFile("/veylith.bad.tmp","/veylith.bad");
}
#endif
#endif

void MyMesh::handleCmdFrame(size_t len) {
  if (cmd_frame[0] == CMD_DEVICE_QUERY && len >= 2) { // sent when app establishes connection
    app_target_ver = cmd_frame[1];                    // which version of protocol does app understand

    int i = 0;
    out_frame[i++] = RESP_CODE_DEVICE_INFO;
    out_frame[i++] = FIRMWARE_VER_CODE;
    out_frame[i++] = MAX_CONTACTS / 2;   // v3+
    out_frame[i++] = MAX_GROUP_CHANNELS; // v3+
    memcpy(&out_frame[i], &_prefs.ble_pin, 4);
    i += 4;
    memset(&out_frame[i], 0, 12);
    strcpy((char *)&out_frame[i], FIRMWARE_BUILD_DATE);
    i += 12;
    StrHelper::strzcpy((char *)&out_frame[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char *)&out_frame[i], FIRMWARE_VERSION, 20);
    i += 20;
    out_frame[i++] = _prefs.client_repeat;   // v9+
    out_frame[i++] = _prefs.path_hash_mode;  // v10+
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_APP_START &&
             len >= 8) { // sent when app establishes connection, respond with node ID
    //  cmd_frame[1..7]  reserved future
    char *app_name = (char *)&cmd_frame[8];
    cmd_frame[len] = 0; // make app_name null terminated
    MESH_DEBUG_PRINTLN("App %s connected", app_name);

    _iter_started = false; // stop any left-over ContactsIterator
    int i = 0;
    out_frame[i++] = RESP_CODE_SELF_INFO;
    out_frame[i++] = ADV_TYPE_CHAT; // what this node Advert identifies as (maybe node's pronouns too?? :-)
    out_frame[i++] = _prefs.tx_power_dbm;
    out_frame[i++] = MAX_LORA_TX_POWER;
    memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat, lon;
    lat = (sensors.node_lat * 1000000.0);
    lon = (sensors.node_lon * 1000000.0);
    memcpy(&out_frame[i], &lat, 4);
    i += 4;
    memcpy(&out_frame[i], &lon, 4);
    i += 4;
    out_frame[i++] = _prefs.multi_acks; // new v7+
    out_frame[i++] = _prefs.advert_loc_policy;
    out_frame[i++] = (_prefs.telemetry_mode_env << 4) | (_prefs.telemetry_mode_loc << 2) |
                     (_prefs.telemetry_mode_base); // v5+
    out_frame[i++] = _prefs.manual_add_contacts;

    uint32_t freq = _prefs.freq * 1000;
    memcpy(&out_frame[i], &freq, 4);
    i += 4;
    uint32_t bw = _prefs.bw * 1000;
    memcpy(&out_frame[i], &bw, 4);
    i += 4;
    out_frame[i++] = _prefs.sf;
    out_frame[i++] = _prefs.cr;

    int tlen = strlen(_prefs.node_name); // revisit: UTF_8 ??
    memcpy(&out_frame[i], _prefs.node_name, tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_TXT_MSG && len >= 14) {
    int i = 1;
    uint8_t txt_type = cmd_frame[i++];
    uint8_t attempt = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    uint8_t *pub_key_prefix = &cmd_frame[i];
    i += 6;
    ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);
    if (recipient && (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND)) {
      char *text = (char *)&cmd_frame[i];
      int tlen = len - i;
      uint32_t est_timeout;
      text[tlen] = 0; // ensure null
      int result;
      uint32_t expected_ack;
      if (txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND) {
        msg_timestamp = getRTCClock()->getCurrentTimeUnique(); // Use node's RTC instead of app timestamp to avoid tripping replay protection
        result = sendCommandData(*recipient, msg_timestamp, attempt, txt_type, text, est_timeout);
        expected_ack = 0; // no Ack expected
      } else {
        result = sendMessage(*recipient, msg_timestamp, attempt, text, expected_ack, est_timeout);
      }
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        if (expected_ack) {
          expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis(); // add to circular table
          expected_ack_table[next_ack_idx].ack = expected_ack;
          expected_ack_table[next_ack_idx].contact = recipient;
          next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
        }

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &expected_ack, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);

#ifdef DISPLAY_CLASS
        // Mirror this app-originated DM into the on-device history too, same as
        // a message composed on-device (MessagesScreen::afterSend) -- otherwise
        // the two queues drift and a DM sent from the phone app never shows up
        // if that same contact is later opened on the device's own screen.
        // ack_tag/ack_deadline mirror the on-device pending->()/x delivery marker
        // too (same "+4s margin over the estimate" the on-device compose path
        // uses); resends stays 0 -- the app already owns its own resend/retry
        // decision, so this only drives the on-screen status, never a second,
        // independent auto-resend from the device itself.
        if (_listener && txt_type == TXT_TYPE_PLAIN) {
          uint32_t ack_deadline_ms = expected_ack ? (millis() + est_timeout + 4000) : 0;
          _listener->addDMMsg(recipient->id.pub_key, true, text, msg_timestamp, expected_ack, ack_deadline_ms, 0);
        }
#endif
      }
    } else {
      writeErrFrame(recipient == NULL
                        ? ERR_CODE_NOT_FOUND
                        : ERR_CODE_UNSUPPORTED_CMD); // unknown recipient, or unsupported TXT_TYPE_*
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_TXT_MSG) { // send GroupChannel text msg
    int i = 1;
    uint8_t txt_type = cmd_frame[i++]; // should be TXT_TYPE_PLAIN
    uint8_t channel_idx = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    const char *text = (char *)&cmd_frame[i];

    if (txt_type != TXT_TYPE_PLAIN) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    } else {
      ChannelDetails channel;
      bool success = getChannel(channel_idx, channel);
      if (success && sendGroupMessage(msg_timestamp, channel.channel, _prefs.node_name, text, len - i)) {
        writeOKFrame();
#ifdef DISPLAY_CLASS
        // Mirror this app-originated channel post into the on-device history
        // (addOwnChannelMsg applies the same "Me: " framing
        // MessagesScreen::afterSend uses for an on-device compose) --
        // otherwise the two queues drift and a post sent from the phone app
        // never shows up if that channel is later opened on-device. text
        // isn't guaranteed null-terminated (len - i is its real length, same
        // bound sendGroupMessage above was just given), so bound the copy.
        // own_message=true (inside addOwnChannelMsg): this is our own post,
        // so it must never bump the channel's unread badge even though the
        // device's own UI isn't necessarily showing this channel right now
        // (unlike an on-device compose, which is always looking at the
        // channel it just sent to). mirrorOwnChannelMsg also arms the
        // "Relayed by" tracker on the new entry -- see its own comment.
        int tlen = len - i;
        if (tlen > MAX_TEXT_LEN) tlen = MAX_TEXT_LEN;
        mirrorOwnChannelMsg(channel_idx, text, tlen, msg_timestamp);
#endif
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
      }
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_DATA) { // send GroupChannel datagram
    if (len < 4) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }
    int i = 1;
    uint8_t channel_idx = cmd_frame[i++];
    uint8_t path_len = cmd_frame[i++];

    // validate path len, allowing 0xFF for flood
    if (!mesh::Packet::isValidPathLen(path_len) && path_len != OUT_PATH_UNKNOWN) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA invalid path size: %d", path_len);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }

    // parse provided path if not flood
    uint8_t path[MAX_PATH_SIZE];
    if (path_len != OUT_PATH_UNKNOWN) {
      i += mesh::Packet::writePath(path, &cmd_frame[i], path_len);
    }

    uint16_t data_type = ((uint16_t)cmd_frame[i]) | (((uint16_t)cmd_frame[i + 1]) << 8);
    i += 2;
    const uint8_t *payload = &cmd_frame[i];
    int payload_len = (len > (size_t)i) ? (int)(len - i) : 0;

    ChannelDetails channel;
    if (!getChannel(channel_idx, channel)) {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    } else if (data_type == DATA_TYPE_RESERVED) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (payload_len > MAX_GROUP_DATA_LENGTH) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA payload too long: %d > %d", payload_len, MAX_GROUP_DATA_LENGTH);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (sendGroupData(channel.channel, path, path_len, data_type, payload, payload_len)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACTS) { // get Contact list
    if (_iter_started) {
      writeErrFrame(ERR_CODE_BAD_STATE); // iterator is currently busy
    } else {
      if (len >= 5) { // has optional 'since' param
        memcpy(&_iter_filter_since, &cmd_frame[1], 4);
      } else {
        _iter_filter_since = 0;
      }

      uint8_t reply[5];
      reply[0] = RESP_CODE_CONTACTS_START;
      uint32_t count = getNumContacts(); // total, NOT filtered count
      memcpy(&reply[1], &count, 4);
      _serial->writeFrame(reply, 5);

      // start iterator
      _iter = startContactsIterator();
      _iter_started = true;
      _iter_target = _serial->replyTarget();
      _most_recent_lastmod = 0;
    }
  } else if (cmd_frame[0] == CMD_SET_ADVERT_NAME && len >= 2) {
    int nlen = len - 1;
    if (nlen > sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1; // max len
    memcpy(_prefs.node_name, &cmd_frame[1], nlen);
    _prefs.node_name[nlen] = 0; // null terminator
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_ADVERT_LATLON && len >= 9) {
    int32_t lat, lon, alt = 0;
    memcpy(&lat, &cmd_frame[1], 4);
    memcpy(&lon, &cmd_frame[5], 4);
    if (len >= 13) {
      memcpy(&alt, &cmd_frame[9], 4); // for FUTURE support
    }
    if (lat <= 90 * 1E6 && lat >= -90 * 1E6 && lon <= 180 * 1E6 && lon >= -180 * 1E6) {
      sensors.node_lat = ((double)lat) / 1000000.0;
      sensors.node_lon = ((double)lon) / 1000000.0;
#ifdef PRIVATE_STATUS_REPORTER
      sensors.position_valid = true;
#endif

      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid geo coordinate
    }
  } else if (cmd_frame[0] == CMD_GET_DEVICE_TIME) {
    uint8_t reply[5];
    reply[0] = RESP_CODE_CURR_TIME;
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply[1], &now, 4);
    _serial->writeFrame(reply, 5);
  } else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t secs;
    memcpy(&secs, &cmd_frame[1], 4);
    uint32_t curr = getRTCClock()->getCurrentTime();
    // Upstream takes only a later time. An hour back is let through: a clock
    // run ahead (a spoofed GPS) is put right by the phone. Further back is
    // done on the device (Settings), which says what it costs: our adverts
    // and logins look old to others until the clock is past the old time.
    if (secs >= curr || curr - secs <= APP_CLOCK_BACK_MAX) {
      setClock(secs);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SEND_SELF_ADVERT) {
    mesh::Packet* pkt;
    pkt = createOwnAdvert();
    if (pkt) {
      if (len >= 2 && cmd_frame[1] == 1) { // optional param (1 = flood, 0 = zero hop)
        unsigned long delay_millis = 0;
        TransportKey default_scope = _scope_list.key(_scope_list.default_idx);   // what messages use too
        sendFloodScoped(default_scope, pkt, delay_millis);
      } else {
        sendZeroHop(pkt);
      }
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_RESET_PATH && len >= 1 + 32) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      recipient->flags &= ~CONTACT_FLAG_ROUTE_PINNED;
      // recipient->lastmod = ??   shouldn't be needed, app already has this version of contact
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // unknown contact
    }
  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    uint32_t last_mod = getRTCClock()->getCurrentTime();  // fallback value if not present in cmd_frame
    if (recipient) {
      uint8_t saved_type = recipient->type; // type is authoritative from advert, not app
      // A route kept on the device (CONTACT_FLAG_ROUTE_PINNED) is the device's
      // own: the app's copy of the flags may predate it. Kept while the app
      // leaves the route as it is; a route the app sets ends it.
      const bool kept = (recipient->flags & CONTACT_FLAG_ROUTE_PINNED) != 0;
      uint8_t held_len = recipient->out_path_len;
      uint8_t held[MAX_PATH_SIZE];
      memcpy(held, recipient->out_path, sizeof(held));
      updateContactFromFrame(*recipient, last_mod, cmd_frame, len);
      if (saved_type != ADV_TYPE_NONE) recipient->type = saved_type;
      int held_bytes = held_len == OUT_PATH_UNKNOWN ? 0 : (held_len & 63) * ((held_len >> 6) + 1);
      const bool same_route = recipient->out_path_len == held_len && held_bytes <= MAX_PATH_SIZE &&
                              memcmp(recipient->out_path, held, held_bytes) == 0;
      if (kept && same_route) recipient->flags |= CONTACT_FLAG_ROUTE_PINNED;
      else recipient->flags &= ~CONTACT_FLAG_ROUTE_PINNED;
      recipient->lastmod = last_mod;
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      ContactInfo contact;
      updateContactFromFrame(contact, last_mod, cmd_frame, len);
      contact.lastmod = last_mod;
      contact.sync_since = 0;
      if (addContact(contact)) {
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_REMOVE_CONTACT) {
    if (deleteContactByKey(&cmd_frame[1])) writeOKFrame();
    else writeErrFrame(ERR_CODE_NOT_FOUND); // not found, or unable to remove
  } else if (cmd_frame[0] == CMD_SHARE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      if (shareContactZeroHop(*recipient)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // unable to send
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACT_BY_KEY) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (contact) {
      writeContactRespFrame(RESP_CODE_CONTACT, *contact);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found
    }
  } else if (cmd_frame[0] == CMD_EXPORT_CONTACT) {
    if (len < 1 + PUB_KEY_SIZE) {
      // export SELF
      mesh::Packet* pkt;
      pkt = createOwnAdvert();
      if (pkt) {
        pkt->header |= ROUTE_TYPE_FLOOD; // would normally be sent in this mode

        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        uint8_t out_len = pkt->writeTo(&out_frame[1]);
        releasePacket(pkt); // undo the obtainNewPacket()
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // Error
      }
    } else {
      uint8_t *pub_key = &cmd_frame[1];
      ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
      uint8_t out_len;
      if (recipient && (out_len = exportContact(*recipient, &out_frame[1])) > 0) {
        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // not found
      }
    }
  } else if (cmd_frame[0] == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
    if (importContact(&cmd_frame[1], len - 1)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SYNC_NEXT_MESSAGE) {
    int out_len;
    if ((out_len = getFromOfflineQueue(out_frame)) > 0) {
      // Taken off the queue for good: every link gets it (an app on Bluetooth
      // and one on USB), as before replies went only to the one that asked.
      const int asked = _serial->replyTarget();
      _serial->setReplyTarget(-1);
      _serial->writeFrame(out_frame, out_len);
      _serial->setReplyTarget(asked);
      if (_listener) _listener->onQueueSizeChanged(offline_queue_len);
    } else {
      out_frame[0] = RESP_CODE_NO_MORE_MESSAGES;
      _serial->writeFrame(out_frame, 1);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_PARAMS) {
    int i = 1;
    uint32_t freq;
    memcpy(&freq, &cmd_frame[i], 4);
    i += 4;
    uint32_t bw;
    memcpy(&bw, &cmd_frame[i], 4);
    i += 4;
    uint8_t sf = cmd_frame[i++];
    uint8_t cr = cmd_frame[i++];
    uint8_t repeat = 0;  // default - false
    if (len > i) {
      repeat = cmd_frame[i++];   // FIRMWARE_VER_CODE  9+
    }

    // Dedicated-band requirement for app-driven repeat disabled, to match the
    // on-device Repeater toggle (Tools > Repeater), which repeats on whatever
    // frequency is already set with no band restriction. Uncomment to restore
    // the old behaviour (repeat=1 only accepted on repeat_freq_ranges, i.e.
    // 433.000/869.495/918.000 MHz exactly, by default).
    // if (repeat && !isValidClientRepeatFreq(freq)) {
    //   writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    // } else
    if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      _prefs.client_repeat = repeat ? 1 : 0;   // a bool for every reader, whatever byte the app sent
      savePrefs();

      applyRepeaterRadio();   // companion params, or the repeater profile if relaying with one set
      // Keep the "repeating ⇒ continuous RX, full TX power" invariants when repeat
      // is toggled via the app, mirroring the on-device path (a repeater must hear
      // all traffic and relay at consistent power).
#if FEAT_RX_POWERSAVE
      radio_driver.setPowerSaving(_prefs.client_repeat ? 0 : _prefs.rx_powersave);
#else
      radio_driver.setPowerSaving(0);   // see Features.h FEAT_RX_POWERSAVE -- ignore any stale persisted rx_powersave byte
#endif
      applyApc();   // pins power to the ceiling; apcActive() keeps it there while repeating
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    int8_t power = (int8_t)cmd_frame[1];
    if (power < -9 || power > MAX_LORA_TX_POWER) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      radio_driver.setTxPower(power);
      // Store what the radio actually applied, not the raw request -- on a
      // board with an external-PA gain curve those can differ (see setTxPower()).
      _prefs.tx_power_dbm = radio_driver.getTxPower();
      if (_prefs.tx_apc) applyApc();   // the new ceiling: re-baseline APC, as the device's own setting does
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SET_TUNING_PARAMS) {
    int i = 1;
    uint32_t rx, af;
    memcpy(&rx, &cmd_frame[i], 4);
    i += 4;
    memcpy(&af, &cmd_frame[i], 4);
    i += 4;
    _prefs.rx_delay_base = ((float)rx) / 1000.0f;
    _prefs.airtime_factor = ((float)af) / 1000.0f;
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_TUNING_PARAMS) {
    uint32_t rx = _prefs.rx_delay_base * 1000, af = _prefs.airtime_factor * 1000;
    int i = 0;
    out_frame[i++] = RESP_CODE_TUNING_PARAMS;
    memcpy(&out_frame[i], &rx, 4); i += 4;
    memcpy(&out_frame[i], &af, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SET_OTHER_PARAMS) {
    _prefs.manual_add_contacts = cmd_frame[1];
    if (len >= 3) {
      _prefs.telemetry_mode_base = cmd_frame[2] & 0x03; // v5+
      _prefs.telemetry_mode_loc = (cmd_frame[2] >> 2) & 0x03;
      _prefs.telemetry_mode_env = (cmd_frame[2] >> 4) & 0x03;

      if (len >= 4) {
        _prefs.advert_loc_policy = cmd_frame[3];
        if (len >= 5) {
          _prefs.multi_acks = cmd_frame[4];
        }
      }
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_PATH_HASH_MODE && cmd_frame[1] == 0 && len >= 3) {
    if (cmd_frame[2] >= 3) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.path_hash_mode = cmd_frame[2];
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_REBOOT && memcmp(&cmd_frame[1], "reboot", 6) == 0) {
    if (!(_listener && _listener->requestShutdown(true))) {
      flushDirtyContacts();
      savePrefs();
      board.reboot();
    }
  } else if (cmd_frame[0] == CMD_GET_BATT_AND_STORAGE) {
    uint8_t reply[11];
    int i = 0;
    reply[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t battery_millivolts = board.getBattMilliVolts();
    uint32_t used = _store->getStorageUsedKb();
    uint32_t total = _store->getStorageTotalKb();
    memcpy(&reply[i], &battery_millivolts, 2); i += 2;
    memcpy(&reply[i], &used, 4); i += 4;
    memcpy(&reply[i], &total, 4); i += 4;
    _serial->writeFrame(reply, i);
  } else if (cmd_frame[0] == CMD_EXPORT_PRIVATE_KEY) {
#if ENABLE_PRIVATE_KEY_EXPORT
    uint8_t reply[65];
    reply[0] = RESP_CODE_PRIVATE_KEY;
    self_id.writeTo(&reply[1], 64);
    _serial->writeFrame(reply, 65);
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if ENABLE_PRIVATE_KEY_IMPORT
    if (!mesh::LocalIdentity::validatePrivateKey(&cmd_frame[1])) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid key
    } else {
        mesh::LocalIdentity identity;
        identity.readFrom(&cmd_frame[1], 64);
        if (_store->saveMainIdentity(identity)) {
          self_id = identity;
          writeOKFrame();
          // re-load contacts, to invalidate ecdh shared_secrets
          resetContacts();
          _store->loadContacts(this);
        } else {
          writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        }
    }
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_SEND_RAW_DATA && len >= 6) {
    int i = 1;
    uint8_t path_len = cmd_frame[i++];   // encoded: hash size in the top bits (upstream 0cce9197 / 67d6d42a)
    if (mesh::Packet::isValidPathLen(path_len)) {
      uint8_t path[MAX_PATH_SIZE];
      i += mesh::Packet::writePath(path, &cmd_frame[i], path_len);
      if (i + 4 > len) {  // min payload 4 bytes
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      } else {
        auto pkt = createRawData(&cmd_frame[i], len - i);
        if (pkt) {
          sendDirect(pkt, path, path_len);
          writeOKFrame();
        } else {
          writeErrFrame(ERR_CODE_TABLE_FULL);
        }
      }
    } else {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // flood, not supported (yet)
    }
  } else if (cmd_frame[0] == CMD_SEND_LOGIN && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    char *password = (char *)&cmd_frame[1 + PUB_KEY_SIZE];
    cmd_frame[len] = 0; // ensure null terminator in password
    if (recipient) {
      uint32_t est_timeout;
      int result = sendLogin(*recipient, password, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        memcpy(&pending_login, recipient->id.pub_key, 4); // match this to onContactResponse()
        strncpy(pending_login_pw, password, sizeof(pending_login_pw) - 1); // saved on success if it's a room
        pending_login_pw[sizeof(pending_login_pw) - 1] = 0;
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &pending_login, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_ANON_REQ && len > 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    ContactInfo anon;
    if (recipient == NULL) { // FIRMWARE_VER_CODE 13+,  allow non-contact requests
      memset(&anon, 0, sizeof(anon));
      memcpy(anon.id.pub_key, pub_key, PUB_KEY_SIZE);
      anon.out_path_len = 0;   // default to zero-hop direct
      anon.type = ADV_TYPE_NONE;  // unknown
      anon.lastmod = getRTCClock()->getCurrentTime();

      if (addContact(anon)) recipient = &anon;
    }
    uint8_t *data = &cmd_frame[1 + PUB_KEY_SIZE];
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendAnonReq(*recipient, data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL); // contacts full
    }
  } else if (cmd_frame[0] == CMD_SEND_STATUS_REQ && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        // FUTURE:  pending_status = tag;  // match this in onContactResponse()
        memcpy(&pending_status, recipient->id.pub_key, 4); // legacy matching scheme
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_PATH_DISCOVERY_REQ && cmd_frame[1] == 0 && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[2];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendDiscoveryReq(*recipient, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_discovery = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len >= 4 + PUB_KEY_SIZE) {  // can deprecate, in favour of CMD_SEND_BINARY_REQ
    uint8_t *pub_key = &cmd_frame[4];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_telemetry = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len == 4) {  // 'self' telemetry request
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    // query other sensors -- target specific
    sensors.querySensors(0xFF, telemetry);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], self_id.pub_key, 6);
    i += 6; // pub_key_prefix
    uint8_t tlen = telemetry.getSize();
    memcpy(&out_frame[i], telemetry.getBuffer(), tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_BINARY_REQ && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint8_t *req_data = &cmd_frame[1 + PUB_KEY_SIZE];
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, req_data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_HAS_CONNECTION && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    if (hasConnectionTo(pub_key)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_LOGOUT && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    stopConnection(pub_key);
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_CHANNEL && len >= 2) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    if (getChannel(channel_idx, channel)) {
      int i = 0;
      out_frame[i++] = RESP_CODE_CHANNEL_INFO;
      out_frame[i++] = channel_idx;
      strcpy((char *)&out_frame[i], channel.name);
      i += 32;
      memcpy(&out_frame[i], channel.channel.secret, 16);
      i += 16; // NOTE: only 128-bit supported
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)&cmd_frame[2], 32);
    memcpy(channel.channel.secret, &cmd_frame[2 + 32], 32); // 256-bit key
    if (setChannelLocal(channel_idx, channel)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)&cmd_frame[2], 32);
    memset(channel.channel.secret, 0, sizeof(channel.channel.secret));
    memcpy(channel.channel.secret, &cmd_frame[2 + 32], 16); // 128-bit key
    if (setChannelLocal(channel_idx, channel)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  } else if (cmd_frame[0] == CMD_SIGN_START) {
    if (sign_data) {
      free(sign_data);
    }
    // A small MCU's heap (nRF52: ~70K, most of it taken at boot) may not have
    // 8K in one block: take the biggest buffer we can get and tell the app its size,
    // rather than promising 8K and failing every SIGN_DATA with BAD_STATE.
    sign_data_len = 0;
    sign_data_cap = MAX_SIGN_DATA_LEN;
    while ((sign_data = (uint8_t *)malloc(sign_data_cap)) == NULL && sign_data_cap > 256) {
      sign_data_cap /= 2;
    }
    if (sign_data == NULL) {
      writeErrFrame(ERR_CODE_BAD_STATE);
    } else {
      out_frame[0] = RESP_CODE_SIGN_START;
      out_frame[1] = 0; // reserved
      memcpy(&out_frame[2], &sign_data_cap, 4);
      _serial->writeFrame(out_frame, 6);
    }
  } else if (cmd_frame[0] == CMD_SIGN_DATA && len > 1) {
    if (sign_data == NULL || sign_data_len + (len - 1) > sign_data_cap) {
      writeErrFrame(sign_data == NULL ? ERR_CODE_BAD_STATE : ERR_CODE_TABLE_FULL); // error: too long
    } else {
      memcpy(&sign_data[sign_data_len], &cmd_frame[1], len - 1);
      sign_data_len += (len - 1);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SIGN_FINISH) {
    if (sign_data) {
      self_id.sign(&out_frame[1], sign_data, sign_data_len);

      free(sign_data); // don't need sign_data now
      sign_data = NULL;

      out_frame[0] = RESP_CODE_SIGNATURE;
      _serial->writeFrame(out_frame, 1 + SIGNATURE_SIZE);
    } else {
      writeErrFrame(ERR_CODE_BAD_STATE);
    }
  } else if (cmd_frame[0] == CMD_SEND_TRACE_PATH && len > 10 && len - 10 < MAX_PACKET_PAYLOAD-5) {
    uint8_t path_len = len - 10;
    uint8_t flags = cmd_frame[9];
    uint8_t path_sz = flags & 0x03;  // NEW v1.11+
    if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) { // make sure is multiple of path_sz
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      uint32_t tag, auth;
      memcpy(&tag, &cmd_frame[1], 4);
      memcpy(&auth, &cmd_frame[5], 4);
      auto pkt = createTrace(tag, auth, flags);
      if (pkt) {
        sendDirect(pkt, &cmd_frame[10], path_len);

        uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
        uint32_t est_timeout = calcDirectTimeoutMillisFor(t, path_len >> path_sz);

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_SET_DEVICE_PIN && len >= 5) {

    // get pin from command frame
    uint32_t pin;
    memcpy(&pin, &cmd_frame[1], 4);

    // ensure pin is zero, or a valid 6 digit pin
    if (pin == 0 || (pin >= 100000 && pin <= 999999)) {
      _prefs.ble_pin = pin;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (i > 0) {
        *dp++ = ',';
      }
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
      bool success = sensors.setSettingValue(sp, np);
      if (success) {
        #if ENV_INCLUDE_GPS == 1
        // Update node preferences for GPS settings
        if (strcmp(sp, "gps") == 0) {
          _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
          savePrefs();
        } else if (strcmp(sp, "gps_interval") == 0) {
          uint32_t interval_seconds = atoi(np);
          _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
          savePrefs();
        }
        #endif
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_ADVERT_PATH && len >= PUB_KEY_SIZE+2) {
    // FUTURE use:  uint8_t reserved = cmd_frame[1];
    uint8_t *pub_key = &cmd_frame[2];
    AdvertPath* found = NULL;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      auto p = &advert_paths[i];
      if (memcmp(p->pubkey_prefix, pub_key, sizeof(p->pubkey_prefix)) == 0) {
        found = p;
        break;
      }
    }
    if (found) {
      int i = 0;
      out_frame[i++] = RESP_CODE_ADVERT_PATH;
      memcpy(&out_frame[i], &found->recv_timestamp, 4); i += 4;
      out_frame[i++] = found->path_len;
      i += mesh::Packet::writePath(&out_frame[i], found->path, found->path_len);
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_STATS && len >= 2) {
    uint8_t stats_type = cmd_frame[1];
    if (stats_type == STATS_TYPE_CORE) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_CORE;
      uint16_t battery_mv = board.getBattMilliVolts();
      uint32_t uptime_secs = _ms->getMillis() / 1000;
      uint8_t queue_len = (uint8_t)_mgr->getOutboundTotal();
      memcpy(&out_frame[i], &battery_mv, 2); i += 2;
      memcpy(&out_frame[i], &uptime_secs, 4); i += 4;
      memcpy(&out_frame[i], &_err_flags, 2); i += 2;
      out_frame[i++] = queue_len;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_RADIO) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_RADIO;
      int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
      int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
      int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4); // scaled by 4 for 0.25 dB precision
      uint32_t tx_air_secs = getTotalAirTime() / 1000;
      uint32_t rx_air_secs = getReceiveAirTime() / 1000;
      memcpy(&out_frame[i], &noise_floor, 2); i += 2;
      out_frame[i++] = last_rssi;
      out_frame[i++] = last_snr;
      memcpy(&out_frame[i], &tx_air_secs, 4); i += 4;
      memcpy(&out_frame[i], &rx_air_secs, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_PACKETS) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_PACKETS;
      uint32_t recv = radio_driver.getPacketsRecv();
      uint32_t sent = radio_driver.getPacketsSent();
      uint32_t n_sent_flood = getNumSentFlood();
      uint32_t n_sent_direct = getNumSentDirect();
      uint32_t n_recv_flood = getNumRecvFlood();
      uint32_t n_recv_direct = getNumRecvDirect();
      uint32_t n_recv_errors = radio_driver.getPacketsRecvErrors();
      memcpy(&out_frame[i], &recv, 4); i += 4;
      memcpy(&out_frame[i], &sent, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_errors, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid stats sub-type
    }
  } else if (cmd_frame[0] == CMD_FACTORY_RESET && memcmp(&cmd_frame[1], "reset", 5) == 0) {
    bool success = factoryReset();   // phone app disconnects before we can send OK frame so it's safe here
    if (success) {
      writeOKFrame();
#ifdef SIM_PLATFORM
      // Skip the pre-reboot UX pause -- board.reboot() just exits the
      // process in the sim (see SimMainBoard::reboot()).
#else
      delay(1000);
#endif
      board.reboot();  // doesn't return
    } else {
      writeErrFrame(ERR_CODE_FILE_IO_ERROR);
    }
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 0) {
    if (len >= 2 + 16) {
      memcpy(send_scope.key, &cmd_frame[2], sizeof(send_scope.key));  // set scope override TransportKey
    } else {
      memset(send_scope.key, 0, sizeof(send_scope.key));  // reset scope override
    }
    send_unscoped = false;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 1) {  // ver 12+
    send_unscoped = true;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_DEFAULT_FLOOD_SCOPE && len >= 1) {
    if (len >= 1+31+16) {
      // strnlen, not strlen: the name field is always 31 bytes in the frame
      // even if the actual name is shorter, so we must bound the search to
      // avoid reading into the key (or past the frame) when no NUL is present.
      int n = (int)strnlen((char *) &cmd_frame[1], 31);
      if (n > 0 && n < 31) {
        // Must go through setPrimaryScope(), not straight into the legacy
        // fields: since the scope list landed, every send and the relay filter
        // resolve through _scope_list, so writing default_scope_name/key alone
        // would leave the app's request with nothing reading it.
        if (!setPrimaryScope((char *) &cmd_frame[1])) {
          writeErrFrame(ERR_CODE_TABLE_FULL);   // no room for a new entry: default unchanged
          return;
        }
        // Honour the key the app derived rather than the one setPrimaryScope()
        // re-derived from the name. They agree today (same "#name" -> SHA256),
        // but the app is the authority on its own regions, and a silent
        // mismatch here would be an on-air difference nothing surfaces.
        memcpy(_scope_list.entries[_scope_list.default_idx - 1].key, &cmd_frame[1+31], 16);
        syncLegacyDefaultScope();
        if (_store) _store->saveScopeList(_scope_list);
        rebuildRepeatScopes();   // slot 0 of the relay filter tracks this key
        savePrefs();
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      setPrimaryScope("");   // clears the legacy fields and points the list back at "*"
      savePrefs();           // setPrimaryScope() writes /scopes1 and rebuilds the relay filter
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_GET_DEFAULT_FLOOD_SCOPE) {
    out_frame[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
    if (strlen(_prefs.default_scope_name) > 0) {
      memcpy(&out_frame[1], _prefs.default_scope_name, 31);
      memcpy(&out_frame[1+31], _prefs.default_scope_key, 16);
      _serial->writeFrame(out_frame, 1+31+16);
    } else {
      _serial->writeFrame(out_frame, 1);   // no name or key means null
    }
  } else if (cmd_frame[0] == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_frame[1] & 0x80) != 0) {
    auto resp = createControlData(&cmd_frame[1], len - 1);
    if (resp) {
      sendZeroHop(resp);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_SET_AUTOADD_CONFIG) {
    _prefs.autoadd_config = cmd_frame[1];
    if (len >= 3) {
      _prefs.autoadd_max_hops = min(cmd_frame[2], (uint8_t)64);
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_AUTOADD_CONFIG) {
    int i = 0;
    out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
    out_frame[i++] = _prefs.autoadd_config;
    out_frame[i++] = _prefs.autoadd_max_hops;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_GET_ALLOWED_REPEAT_FREQ) {
    int i = 0;
    out_frame[i++] = RESP_ALLOWED_REPEAT_FREQ;
    for (int k = 0; k < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]) && i + 8 < sizeof(out_frame); k++) {
      auto r = &repeat_freq_ranges[k];
      memcpy(&out_frame[i], &r->lower_freq, 4); i += 4;
      memcpy(&out_frame[i], &r->upper_freq, 4); i += 4;
    }
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_RUN_CLI_COMMAND && len >= 3) {   // v14+, MyMeshCli.h
    char text[MAX_FRAME_SIZE + 1];
    memcpy(text, &cmd_frame[1], len - 1);
    text[len - 1] = 0;
    char* reply = (char*)&out_frame[1];
    const int n = MAX_FRAME_SIZE - 1;
    reply[0] = 0;
    if (!handleCliCommand(text, reply, n, true)) {
      size_t at = strlen(reply);   // after an "xx|" prefix, if it had one
      snprintf(reply + at, n - at, "Unknown command");
    }
    out_frame[0] = RESP_CODE_CLI_REPLY;
    _serial->writeFrame(out_frame, 1 + strlen(reply));
  } else if (cmd_frame[0] == CMD_SEND_RAW_PACKET && len >= 4) {
    auto pkt = obtainNewPacket();
    if (pkt) {
      uint8_t priority = cmd_frame[1];
      if (tryParsePacket(pkt, &cmd_frame[2], len - 2)) {
        sendPacket(pkt, priority, 0);
        writeOKFrame();
      } else {
        releasePacket(pkt);
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
#if FEAT_ROUTES
  } else if (cmd_frame[0] == CMD_ROUTE) {
    handleRouteCommand(cmd_frame, len);
#endif
#ifdef ENABLE_SCREENSHOT
  } else if (cmd_frame[0] == CMD_GET_SCREENSHOT) {
    handleScreenshotRequest();
#endif
  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
  }
}

static bool save_filter(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE;   // don't save the transient/anon entries
}

void MyMesh::saveContacts() {
  _store->saveContacts(this, save_filter);
}

void MyMesh::enterCLIRescue() {
  _cli_rescue = true;
  cli_command[0] = 0;
  Serial.println("========= CLI Rescue =========");
}

// Upstream's CLI entry (the rescue CLI, a remote contact's CLI message): our
// companion CLI (MyMeshCli.h) answers, as it does CMD_RUN_CLI_COMMAND. `reply`
// is upstream's buffer: 160 bytes free.
bool MyMesh::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  (void)sender_timestamp;
  return handleCliCommand(command, reply, 160);
}

void MyMesh::checkCLIRescueCmd() {
  int len = strlen(cli_command);
  // `cli_command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(cli_command) and the loop below would
  // then index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(cli_command)) {
    cli_command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(cli_command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      cli_command[len++] = c;
      cli_command[len] = 0;
    }
    Serial.print(c);  // echo
  }
  if (len == sizeof(cli_command)-1) {  // buffer full: treat as a completed line
    cli_command[sizeof(cli_command)-2] = '\r';  // place end-of-line marker inside the buffer
    cli_command[sizeof(cli_command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && cli_command[len - 1] == '\r') {  // received complete line
    cli_command[len - 1] = 0;  // replace newline with C string null terminator

    reply_buf[0] = 0;
    if (handleCliCommand(cli_command, reply_buf, sizeof(reply_buf), true)) {
      // command was handled, print reply output
      Serial.print("  "); Serial.print(reply_buf); Serial.println();
    } else if (strcmp(cli_command, "rebuild") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        _store->saveMainIdentity(self_id);
        savePrefs();
        saveContacts();
        saveChannels();
        Serial.println("  > erase and rebuild done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (strcmp(cli_command, "erase") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        Serial.println("  > erase done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (memcmp(cli_command, "ls", 2) == 0) {

      // get path from command e.g: "ls /adafruit"
      const char *path = &cli_command[3];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }
      Serial.printf("Listing files in %s\n", path);

      // log each file and directory
      File root = _store->openRead(path);
      if (is_fs2 == false) {
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  UserData%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] UserData%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root.openNextFile();
          }
          root.close();
        }
      }

      if (is_fs2 == true || strlen(path) == 0 || strcmp(path, "/") == 0) {
        if (_store->getSecondaryFS() != nullptr) {
          File root2 = _store->openRead(_store->getSecondaryFS(), path);
          File file = root2.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  ExtraFS%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] ExtraFS%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root2.openNextFile();
          }
          root2.close();
        }
      }
    } else if (memcmp(cli_command, "cat", 3) == 0) {

      // get path from command e.g: "cat /contacts3"
      const char *path = &cli_command[4];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      } else {
        Serial.println("Invalid path provided, must start with UserData/ or ExtraFS/");
        cli_command[0] = 0;
        return;
      }

      // log file content as hex
      File file = _store->openRead(path);
      if (is_fs2 == true) {
        file = _store->openRead(_store->getSecondaryFS(), path);
      }
      if(file){

        // get file content
        int file_size = file.available();
        uint8_t buffer[file_size];
        file.read(buffer, file_size);

        // print hex
        mesh::Utils::printHex(Serial, buffer, file_size);
        Serial.print("\n");

        file.close();

      }

    } else if (memcmp(cli_command, "rm ", 3) == 0) {
      // get path from command e.g: "rm /adv_blobs"
      const char *path = &cli_command[3];
      MESH_DEBUG_PRINTLN("Removing file: %s", path);
      // ensure path is not empty, or root dir
      if(!path || strlen(path) == 0 || strcmp(path, "/") == 0){
        Serial.println("Invalid path provided");
      } else {
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }

        // remove file
        bool removed;
        if (is_fs2) {
          MESH_DEBUG_PRINTLN("Removing file from ExtraFS: %s", path);
          removed = _store->removeFile(_store->getSecondaryFS(), path);
        } else {
          MESH_DEBUG_PRINTLN("Removing file from UserData: %s", path);
          removed = _store->removeFile(path);
        }
        if(removed){
          Serial.println("File removed");
        } else {
          Serial.println("Failed to remove file");
        }

      }

    } else if (strcmp(cli_command, "reboot") == 0) {
      if (!(_listener && _listener->requestShutdown(true))) {
        flushDirtyContacts();
        savePrefs();  // flush any on-device setting change not yet persisted -- see UITask::shutdown()'s comment
        board.reboot();  // doesn't return
      }
    } else {
      Serial.println("  Error: unknown command");
    }

    cli_command[0] = 0;  // reset command buffer
  }
}

void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
#ifdef PRIVATE_STATUS_REPORTER
  char command[80];
  if (_serial->checkRecvText(command, sizeof(command))) {
    char reply[160];
    if (!handleStatusCommand(command, reply)) strcpy(reply, "Error: unknown tracker command; use status help");
    _serial->writeText(reply);
  }
#endif
  if (len > 0) {
    handleCmdFrame(len);
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    // The list goes on to the link that asked for it, whatever came in since.
    int prev_target = _serial->replyTarget();
    _serial->setReplyTarget(_iter_target);
    ContactInfo contact;
    if (_iter.hasNext(this, contact)) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
        if (contact.lastmod > _most_recent_lastmod) {
          _most_recent_lastmod = contact.lastmod; // save for the RESP_CODE_END_OF_CONTACTS frame
        }
      }
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
      _serial->writeFrame(out_frame, 5);
      _iter_started = false;
    }
    _serial->setReplyTarget(prev_target);
  //} else if (!_serial->isWriteBusy()) {
  //  checkConnections();    // TODO - deprecate the 'Connections' stuff
  }
}

void MyMesh::loop() {
  BaseChatMesh::loop();

  // APC: a tracked channel/flood send that no repeater echoed within the window is
  // treated as a lost confirmation → ramp power up (lets channel sends recover).
  if (_apc_flood_pending && millisHasNowPassed(_apc_flood_deadline)) {
    _apc_flood_pending = false;
    if (apcActive()) apcOnFailure();
  }
  // UI relay windows expired with no echo — just drop them (no echo is not a
  // failure for channels; the marker simply stays "sent").
  if (_relay_active > 0) {
    for (int i = 0; i < RELAY_RING; i++) {
      if (_relay[i].pending && millisHasNowPassed(_relay[i].deadline)) {
        _relay[i].pending = false;
        _relay_active--;
      }
    }
  }

  tickLocFix();
#ifdef PRIVATE_STATUS_REPORTER
  trackerLoop();
#endif

  if (_cli_batch && millisHasNowPassed(_batch_until)) endCliBatch();   // the page went quiet mid-batch

  if (_cli_rescue) {
    checkCLIRescueCmd();
  } else {
    checkSerialInterface();
#if defined(ENABLE_WIFI_INTERFACE) && defined(RP2040_PLATFORM) && !defined(ENABLE_USB_INTERFACE)
    // RP2040 WiFi builds are headless and have no way into the rescue CLI (that needs a
    // display + long-press), so serve config commands on the otherwise unused USB serial
    checkCLIRescueCmd();
#endif
  }

  // is there are pending dirty contacts write needed?
  // Rewriting the whole file blocks for up to a second or two on the nRF52:
  // put off while the user is pressing keys.
  if ((dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry))
      || (dirty_paths_expiry && millisHasNowPassed(dirty_paths_expiry))) {
    if (millis() - _last_input_ms < USER_QUIET_MILLIS) {
      unsigned long later = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      if (dirty_contacts_expiry) dirty_contacts_expiry = later;
      if (dirty_paths_expiry) dirty_paths_expiry = later;
    } else {
      saveContacts();
      dirty_contacts_expiry = dirty_paths_expiry = 0;
    }
  }

  if (millis() - _last_rtc_save_ms > RTC_SAVE_INTERVAL_MILLIS && millis() - _last_input_ms >= USER_QUIET_MILLIS) {
    _store->saveRTCTime();
    _last_rtc_save_ms = millis();
  }

  if (_prefs.advert_auto_interval_sec != _auto_advert_sec) {
    // Interval changed (Settings, CLI, app): count the new one from the last
    // advert instead of waiting out what was left of the old one.
    bool first = _auto_advert_sec == UINT32_MAX;   // boot: keep the advert due straight away
    _auto_advert_sec = _prefs.advert_auto_interval_sec;
    if (!first) _next_auto_advert_ms = _last_auto_advert_ms + _auto_advert_sec * 1000UL;
  }
  if (_prefs.advert_auto_interval_sec > 0 && millisHasNowPassed(_next_auto_advert_ms)) {
    // The position only where Privacy lets adverts carry it, at its precision.
    // Skipped (not put off) while an SOS adverts its own.
    mesh::Packet* pkt = _auto_advert_held ? nullptr : createOwnAdvert();
    if (pkt) sendZeroHop(pkt);
    _last_auto_advert_ms = _ms->getMillis();
    _next_auto_advert_ms = futureMillis(_prefs.advert_auto_interval_sec * 1000UL);
  }
}

// Grid step in degrees per precision level. 1 deg lat ~ 111 km, so 0.0009 ~ 100 m,
// 0.0045 ~ 500 m, 0.009 ~ 1 km (longitude steps shrink toward the poles, so they're
// never coarser).
// Snapping to a fixed grid (rather than adding random jitter) means repeated
// adverts from one spot always send the same value, so they can't be averaged.
static const double ADV_LOC_PREC_STEPS[NodePrefs::ADV_LOC_PREC_COUNT] = { 0.0, 0.0009, 0.0045, 0.009 };

static double snapToGrid(double v, double step) {
  if (step <= 0.0) return v;
  return floor(v / step + 0.5) * step;
}

mesh::Packet* MyMesh::createOwnAdvert() {
  if (
#ifdef PRIVATE_STATUS_REPORTER
      true ||
#endif
      _prefs.advert_loc_policy == ADVERT_LOC_NONE
      || (sensors.node_lat == 0 && sensors.node_lon == 0)) {
    return createSelfAdvert(_prefs.node_name);
  }
  const uint8_t prec = _prefs.advert_loc_prec;
  const double step = ADV_LOC_PREC_STEPS[prec < NodePrefs::ADV_LOC_PREC_COUNT ? prec : 0];
  return createSelfAdvert(_prefs.node_name,
                          snapToGrid(sensors.node_lat, step),
                          snapToGrid(sensors.node_lon, step));
}

bool MyMesh::advert() {
  mesh::Packet* pkt;
  pkt = createOwnAdvert();
  if (pkt) {
    sendZeroHop(pkt);
    return true;
  } else {
    return false;
  }
}

bool MyMesh::advertFlood() {
  // Mirrors the CMD_SEND_SELF_ADVERT handler's flood=1 branch above
  // (createSelfAdvert() + sendFloodScoped() with the default transport
  // scope key), for the on-device UI and the sim's test harness.
  mesh::Packet* pkt;
  pkt = createOwnAdvert();
  if (pkt) {
    TransportKey default_scope = _scope_list.key(_scope_list.default_idx);
    sendFloodScoped(default_scope, pkt, 0);
    return true;
  } else {
    return false;
  }
}

bool MyMesh::advertSos(bool with_pos, int32_t lat_1e6, int32_t lon_1e6, bool flood) {
  mesh::Packet* pkt = with_pos ? createSelfAdvert(_prefs.node_name, lat_1e6 / 1e6, lon_1e6 / 1e6)
                               : createSelfAdvert(_prefs.node_name);
  if (!pkt) return false;
  if (flood) sendFloodScoped(_scope_list.key(_scope_list.default_idx), pkt, 0);   // as the app's flood advert
  else sendZeroHop(pkt);
  return true;
}

// Sets the clock and saves it for the next start (/rtc_save).
void MyMesh::setClock(uint32_t utc) {
  const bool back = utc < getRTCClock()->getCurrentTime();
  getRTCClock()->setCurrentTime(utc);
  saveRTCTime();
  if (!back) return;
  // Contacts heard while the clock ran ahead carry those times; left so, the
  // next start would take the clock forward to them (bootstrapRTCfromContacts).
  bool any = false;
  for (int i = 0; i < getNumContacts(); i++) {
    ContactInfo c;
    if (!getContactByIdx(i, c) || c.lastmod <= utc) continue;
    ContactInfo* p = lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
    if (p) { p->lastmod = utc; any = true; }
  }
  if (any) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

// To check if there is pending work
// Work for the loop now: what it mustn't nap through. A packet queued for
// later (a flood's random delay, a retry) or the contacts' lazy write
// pending (5 s after each change, and put off while keys are pressed) isn't:
// the loop's naps are short (an nRF52 wakes on its 1 kHz tick, the L2 within
// 50 ms) and meet them when due. Counting them kept the loop from sleeping at
// all for seconds after every advert from a known contact.
bool MyMesh::hasPendingWork() const {
  return _mgr->getOutboundCount(_ms->getMillis()) > 0 ||
         (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry));
}

#include "MyMeshContacts.h"
#include "MyMeshScope.h"
#include "MyMeshPing.h"
#include "MyMeshApc.h"
#include "MyMeshScreenshot.h"
#include "MyMeshRoutes.h"
#include "MyMeshBot.h"
#include "MyMeshCli.h"

#ifdef PRIVATE_STATUS_REPORTER
void MyMesh::loadTrackerPrefs() {
  auto file = _store->openRead("/zygis1");
  if (!file) return;
  uint8_t data[74];
  bool valid = file.read(data, sizeof(data)) == sizeof(data);
  file.close();
  if (!valid || memcmp(data, "ZYG1", 4)) return;
  uint32_t seconds = uint32_t(data[4]) | (uint32_t(data[5]) << 8) |
                     (uint32_t(data[6]) << 16) | (uint32_t(data[7]) << 24);
  if (seconds > 86400 || (data[8] != 255 && data[8] >= MAX_GROUP_CHANNELS) ||
      data[9] > 1 || !memchr(data + 10, 0, 32)) return;
  tracker_prefs.status_interval = seconds;
  tracker_prefs.status_channel = data[8];
  tracker_prefs.status_position_known = data[9];
  // Older records may contain unused nonzero bytes after the name terminator.
  strncpy(tracker_prefs.status_name, (const char*)data + 10, sizeof(tracker_prefs.status_name));
  memcpy(tracker_prefs.status_key, data + 42, 32);
}

void MyMesh::saveTrackerPrefs() {
  tracker_prefs.status_position_known = sensors.position_valid;
  uint8_t data[74] = {'Z', 'Y', 'G', '1'};
  uint32_t seconds = tracker_prefs.status_interval;
  for (unsigned i = 0; i < 4; ++i) data[4 + i] = uint8_t(seconds >> (8 * i));
  data[8] = tracker_prefs.status_channel;
  data[9] = tracker_prefs.status_position_known;
  // ChannelDetails names are NUL-terminated but not necessarily zero-padded.
  // Canonicalize the stored name so valid bindings survive a reboot.
  strncpy((char*)data + 10, tracker_prefs.status_name, sizeof(tracker_prefs.status_name) - 1);
  memcpy(data + 42, tracker_prefs.status_key, 32);
  auto file = _store->openWrite("/zygis1.tmp");
  if (!file) return;
  bool ok = file.write(data, sizeof(data)) == sizeof(data);
  file.close();
  if (ok) _store->commitFile("/zygis1.tmp", "/zygis1");
}

// Never print the PSK. Also reject public/name-derived keys under renamed slots.
const char* MyMesh::privateStatusChannel(const ChannelDetails& ch) {
  static const uint8_t public_key[16] = {0x8b,0x33,0x87,0xe9,0xc5,0xcd,0xea,0x6a,0xc9,0xe5,0xed,0xba,0xa1,0x15,0xcd,0x72};
  static const uint8_t zero[32] = {};
  if (!ch.name[0]) return "empty channel";
  if (ch.name[0] == '#') return "hashtag channel";
  if (!memcmp(ch.channel.secret, zero, 32)) return "empty key";
  if (!memcmp(ch.channel.secret, public_key, 16) && !memcmp(ch.channel.secret + 16, zero, 16)) return "Public key";
  char hashtag[34];
  snprintf(hashtag, sizeof(hashtag), "#%s", ch.name);
  uint8_t derived[32];
  mesh::Utils::sha256(derived, 32, (const uint8_t*)hashtag, strlen(hashtag));
  if (!memcmp(ch.channel.secret, derived, 16)) return "hashtag-derived key";
  mesh::Utils::sha256(derived, 32, (const uint8_t*)ch.name, strlen(ch.name));
  if (!memcmp(ch.channel.secret, derived, 16)) return "name-derived key";
  return nullptr;
}

const char* MyMesh::verifyTrackerChannel(ChannelDetails& ch) {
  if (!getChannel(tracker_prefs.status_channel, ch)) return "select a private channel";
  const char* error = privateStatusChannel(ch);
  if (error) return error;
  if (memcmp(ch.channel.secret, tracker_prefs.status_key, 32) ||
      strncmp(ch.name, tracker_prefs.status_name, 32)) return "channel changed; select it again";
  return nullptr;
}

const char* MyMesh::verifyStatus(ChannelDetails& ch) {
  const char* error = verifyTrackerChannel(ch);
  if (error) return error;
  if (sensors.position_valid && (!isfinite(sensors.node_lat) || !isfinite(sensors.node_lon) ||
      sensors.node_lat < -90 || sensors.node_lat > 90 ||
      sensors.node_lon < -180 || sensors.node_lon > 180)) return "invalid known position";
  return nullptr;
}

bool MyMesh::trackerReply(const char* text, bool position) {
  // One help packet fits even with a 31-byte sender name: 99 + 33 = 132.
  if (strlen(text) > 100 || tracker_reply_count == 24) return false;
  auto& reply = tracker_replies[(tracker_reply_head + tracker_reply_count) % 24];
  strcpy(reply.text, text);
  reply.position = position;
  ++tracker_reply_count;
  return true;
}

void MyMesh::flushTrackerReplies() {
  if (!tracker_reply_count || (int32_t)(millis() - next_tracker_reply) < 0) return;
  ChannelDetails ch;
  if (verifyStatus(ch)) return;
  auto& reply = tracker_replies[tracker_reply_head];
#ifdef TECHO_STATUS_LEDS
  building_status = reply.position;
#endif
  bool queued = sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), ch.channel, _prefs.node_name,
                                 reply.text, strlen(reply.text));
#ifdef TECHO_STATUS_LEDS
  building_status = false;
#endif
  if (queued) {
    tracker_reply_head = (tracker_reply_head + 1) % 24;
    --tracker_reply_count;
    next_tracker_reply = millis() + 1000;
  }
}

bool MyMesh::sendStatus() {
  ChannelDetails ch;
  if (verifyStatus(ch)) return false;
#ifndef BATT_MIN_MILLIVOLTS
#define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
#define BATT_MAX_MILLIVOLTS 4200
#endif
  int percent = constrain(((int)board.getBattMilliVolts() - BATT_MIN_MILLIVOLTS) * 100 /
                          (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS), 0, 100);
  auto location = sensors.getLocationProvider();
  bool fresh = acquiring_position && location && location->hasFreshFix(15000);
  char text[101];
  if (sensors.position_valid) {
    snprintf(text, sizeof(text), "Bat:%d%% GPS:%s https://maps.google.com/?q=%.6f,%.6f", percent,
             fresh ? "fix" : "no fix; last known", sensors.node_lat, sensors.node_lon);
  } else snprintf(text, sizeof(text), "Bat:%d%% GPS:no fix; no last known position. %s", percent,
                  acquiring_position ? "Acquiring." : "Listening.");
  bool queued = trackerReply(text, sensors.position_valid);
  if (queued && sensors.position_valid && (!tracker_prefs.status_position_known ||
      (uint32_t)(millis() - last_position_save) >= 300000UL)) {
    savePrefs();
    last_position_save = millis();
  }
  return queued;
}

void MyMesh::startPositionRequest() {
  if (acquiring_position) return;
  acquiring_position = sensors.setGPSFromCLI(true);
  next_status = millis() + tracker_prefs.status_interval * 1000UL;
}

void MyMesh::trackerLoop() {
  if (_prefs.gps_enabled && tracker_prefs.status_interval > 0 && tracker_prefs.status_interval <= 86400 &&
      (int32_t)(millis() - next_status) >= 0) {
    next_status = millis() + tracker_prefs.status_interval * 1000UL;
    startPositionRequest();
    auto location = sensors.getLocationProvider();
    if (!location || !location->hasFreshFix(15000)) sendStatus();
  }
  auto location = sensors.getLocationProvider();
  if (acquiring_position && location && location->hasFreshFix(15000)) {
    // Read this new fix directly; the sensor's periodic cache update may not have run yet.
    sensors.node_lat = location->getLatitude() / 1000000.0;
    sensors.node_lon = location->getLongitude() / 1000000.0;
    sensors.position_valid = true;
    if (sendStatus()) {
      acquiring_position = false;
      sensors.setGPSFromCLI(false);
      next_status = millis() + tracker_prefs.status_interval * 1000UL;
    }
  }
  nearbyLoop();
  flushTrackerReplies();
}

void MyMesh::replyRoute(mesh::Packet* packet, const char* command) {
  if (!packet->isRouteFlood() || !packet->getPathHashCount()) {
    char text[101];
    snprintf(text, sizeof(text), "ACK %s; route: direct (no repeater hashes).", command);
    trackerReply(text);
    return;
  }
  char text[101];
  snprintf(text, sizeof(text), "ACK %s; route hashes: ", command);
  for (unsigned i = 0; i < packet->getPathHashCount(); ++i) {
    if (strlen(text) + packet->getPathHashSize() * 2 + 1 > 100) {
      trackerReply(text);
      strcpy(text, "Route continued: ");
    }
    char* out = text + strlen(text);
    for (unsigned j = 0; j < packet->getPathHashSize(); ++j) {
      sprintf(out, "%02X", packet->path[i * packet->getPathHashSize() + j]);
      out += 2;
    }
    *out++ = i + 1 < packet->getPathHashCount() ? '>' : 0;
    *out = 0;
  }
  trackerReply(text);
}

void MyMesh::handleTrackerMessage(const mesh::GroupChannel& channel, mesh::Packet* packet, const char* text) {
  ChannelDetails ch;
  if (verifyTrackerChannel(ch) || memcmp(channel.secret, ch.channel.secret, 32)) return;
  const char* body = strstr(text, ": "); // Group text is '<sender>: <body>'.
  if (!body) return;
  auto request = tracker::parse(body + 2, _prefs.node_name);
  switch (request.action) {
    case tracker::Action::Locate:
      replyRoute(packet, "!kur");
      startPositionRequest();
      if (!sensors.getLocationProvider() || !sensors.getLocationProvider()->hasFreshFix(15000)) sendStatus();
      break;
    case tracker::Action::PowerDown:
      _prefs.gps_enabled = 0;
      acquiring_position = false;
      sensors.setGPSFromCLI(false);
      savePrefs();
      trackerReply("Power saving ON; GPS stopped. Listening/responding.");
      break;
    case tracker::Action::PowerUp:
      _prefs.gps_enabled = 1;
      savePrefs();
      startPositionRequest();
      trackerReply("Power saving OFF; GPS tracking resumed.");
      if (!sensors.getLocationProvider() || !sensors.getLocationProvider()->hasFreshFix(15000)) sendStatus();
      break;
    case tracker::Action::PowerState:
      trackerReply(_prefs.gps_enabled ? "Power saving OFF; tracking enabled (GPS sleeps between fixes)." :
                                       "Power saving ON; tracking paused; listening/responding.");
      break;
    case tracker::Action::Interval: {
      uint32_t seconds = 0;
      bool valid = true;
      for (const char* p = request.text; *p; ++p) {
        if (*p < '0' || *p > '9' || seconds > 8640) { valid = false; break; }
        seconds = seconds * 10 + (*p - '0');
        if (seconds > 86400) { valid = false; break; }
      }
      char reply[80];
      if (!valid) trackerReply("Invalid interval. Use !interval 0..86400 seconds (0=timer off).");
      else {
        if (*request.text) {
          tracker_prefs.status_interval = seconds;
          next_status = millis() + seconds * 1000UL;
          savePrefs();
        }
        snprintf(reply, sizeof(reply), "Interval:%lu seconds (0=scheduled reports off).", (unsigned long)tracker_prefs.status_interval);
        trackerReply(reply);
      }
      break;
    }
    case tracker::Action::Help: trackerReply(tracker::HELP); break;
    case tracker::Action::Triangulate:
      replyRoute(packet, "!triangulate");
      startNearbyDiscovery();
      break;
    case tracker::Action::Alarm:
      replyRoute(packet, "!tikra");
      if (_listener) _listener->onTrackerAlert(*request.text ? request.text : "ALARM !tikra", true);
      break;
    case tracker::Action::Conversation:
      if (_listener) _listener->onTrackerAlert(request.text, false);
      break;
    case tracker::Action::Unknown: {
      char token[25] = {};
      size_t len = strcspn(request.text, " \t");
      if (len > sizeof(token) - 1) len = sizeof(token) - 1;
      memcpy(token, request.text, len);
      replyRoute(packet, token);
      break;
    }
    default: break;
  }
}

void MyMesh::startNearbyDiscovery() {
  if (nearby_running) { trackerReply("!triangulate: discovery already running."); return; }
  getRNG()->random((uint8_t*)&nearby_tag, sizeof(nearby_tag));
  uint8_t data[6] = { uint8_t(CTL_TYPE_NODE_DISCOVER_REQ | 1), uint8_t(1 << ADV_TYPE_REPEATER) };
  memcpy(data + 2, &nearby_tag, 4);
  auto packet = createControlData(data, sizeof(data));
  if (!packet) { trackerReply("!triangulate: radio queue full; retry."); return; }
  nearby_count = nearby_reported = 0;
  nearby_overflow = false;
  nearby_running = true;
  nearby_deadline = 0; // Window starts after actual transmission, not while queued.
  nearby_request = packet;
  sendZeroHop(packet);
}

void MyMesh::handleNearbyResponse(mesh::Packet* packet) {
  if (!nearby_running || !nearby_deadline || (int32_t)(millis() - nearby_deadline) >= 0 ||
      packet->payload_len < 14 || !packet->isRouteDirect() || packet->getPathHashCount() != 0 ||
      packet->payload[0] != (CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER) ||
      memcmp(packet->payload + 2, &nearby_tag, 4)) return;
  for (unsigned i = 0; i < nearby_count; ++i) {
    if (!memcmp(nearby_repeaters[i].key, packet->payload + 6, 8)) return;
  }
  if (nearby_count == 64) { nearby_overflow = true; return; }
  auto& repeater = nearby_repeaters[nearby_count++];
  memcpy(repeater.key, packet->payload + 6, 8);
  repeater.rssi = (int16_t)_radio->getLastRSSI();
  repeater.snr_in = packet->_snr;
  repeater.snr_out = packet->payload[1];
}

void MyMesh::nearbyLoop() {
  if (!nearby_running || !nearby_deadline || (int32_t)(millis() - nearby_deadline) < 0) return;
  while (nearby_reported < nearby_count && tracker_reply_count < 23) {
    auto& repeater = nearby_repeaters[nearby_reported];
    char key[17];
    for (unsigned i = 0; i < 8; ++i) sprintf(key + 2*i, "%02X", repeater.key[i]);
    ContactInfo* contact = lookupContactByPubKey(repeater.key, 8);
    char text[101];
    snprintf(text, sizeof(text), "Direct %u: %.31s [%s] RX:%ddBm SNR in/out:%+.1f/%+.1fdB", nearby_reported + 1,
             contact ? contact->name : "unknown", key, repeater.rssi, repeater.snr_in / 4.0, repeater.snr_out / 4.0);
    if (!trackerReply(text)) return;
    ++nearby_reported;
  }
  if (nearby_reported == nearby_count && tracker_reply_count < 24) {
    char text[101];
    snprintf(text, sizeof(text), "!triangulate: %u direct repeater replies in 15s.%s", nearby_count,
             nearby_overflow ? " Limit 64 reached; more replies omitted." : " Silent/hidden nodes cannot be listed.");
    if (trackerReply(text)) nearby_running = false;
  }
}

static bool statusNumber(const char* text, uint32_t max, uint32_t& value) {
  if (!*text) return false;
  value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    value = value * 10 + (*text - '0');
    if (value > max) return false;
  }
  return true;
}

bool MyMesh::handleStatusCommand(const char* cmd, char* reply) {
  if (!strcmp(cmd, "exit")) {
    _cli_rescue = false;
    strcpy(reply, "Companion USB protocol resumed");
  } else if (!strcmp(cmd, "help") || !strcmp(cmd, "status help")) {
    strcpy(reply, "gps on|off|status|diag; status channels [slot|page N]; status channel <slot>; status interval <0..86400 sec>; status show; status send; verify (0=stop)");
  } else if (!strcmp(cmd, "gps on") || !strcmp(cmd, "gps off")) {
    bool enabled = !strcmp(cmd, "gps on");
    _prefs.gps_enabled = enabled;
    if (enabled) startPositionRequest();
    else { acquiring_position = false; sensors.setGPSFromCLI(false); }
    savePrefs();
    strcpy(reply, enabled ? "Tracking on; GPS cycles per fix; CLI locked" : "Tracking off; GPS off; CLI locked");
  } else if (!strcmp(cmd, "tracker hardware")) {
    snprintf(reply, 160, "Audio:%s GPS:%s; tracking:%s", trackerAudioAvailable() ? "available" : "missing",
             sensors.isGPSDetected() ? "detected" : "missing", _prefs.gps_enabled ? "on" : "off");
  } else if (!strcmp(cmd, "gps diag")) {
    auto location = sensors.getLocationProvider();
    if (location) location->formatDiagnostics(reply, 160);
    else strcpy(reply, "No GPS provider");
  } else if (!strcmp(cmd, "gps status")) {
    sprintf(reply, "Tracking %s; GPS %s; CLI locked", _prefs.gps_enabled ? "on" : "off", acquiring_position ? "acquiring" : "asleep");
  } else if (!strcmp(cmd, "status channels") || !strncmp(cmd, "status channels page ", 21)) {
    uint32_t page = 0;
    if (strcmp(cmd, "status channels") && !statusNumber(cmd + 21, (MAX_GROUP_CHANNELS - 1) / 3, page)) {
      strcpy(reply, "Error: invalid page"); return true;
    }
    snprintf(reply, 160, "Page %lu (status channels page N):", (unsigned long)page);
    for (unsigned i = page * 3; i < page * 3 + 3 && i < MAX_GROUP_CHANNELS; ++i) {
      ChannelDetails ch;
      if (getChannel(i, ch) && ch.name[0]) snprintf(reply + strlen(reply), 160 - strlen(reply), "\n%u: %s", i, ch.name);
    }
  } else if (!strncmp(cmd, "status channels ", 16) || !strncmp(cmd, "status channel ", 15)) {
    bool inspect = !strncmp(cmd, "status channels ", 16);
    uint32_t slot;
    ChannelDetails ch;
    if (!statusNumber(cmd + (inspect ? 16 : 15), MAX_GROUP_CHANNELS - 1, slot) || !getChannel(slot, ch) || !ch.name[0]) {
      strcpy(reply, "Error: invalid or empty channel slot");
    } else {
      const char* error = privateStatusChannel(ch);
      if (inspect) snprintf(reply, 160, "%lu: %s (%s)", (unsigned long)slot, ch.name, error ? error : "private candidate");
      else if (error) snprintf(reply, 160, "Error: %s", error);
      else {
        tracker_reply_count = tracker_reply_head = 0; // Never redirect queued replies to a newly selected channel.
        nearby_running = false; nearby_request = nullptr;
        tracker_prefs.status_channel = slot;
        memcpy(tracker_prefs.status_key, ch.channel.secret, 32);
        memcpy(tracker_prefs.status_name, ch.name, 32);
        savePrefs();
        snprintf(reply, 160, "Status channel: %lu %s", (unsigned long)slot, ch.name);
      }
    }
  } else if (!strncmp(cmd, "status interval ", 16)) {
    uint32_t seconds;
    if (!statusNumber(cmd + 16, 86400, seconds)) strcpy(reply, "Error: interval must be 0..86400 seconds");
    else {
      tracker_prefs.status_interval = seconds;
      next_status = millis() + seconds * 1000UL;
      savePrefs();
      sprintf(reply, "Status interval: %lu seconds (0=disabled)", (unsigned long)seconds);
    }
  } else if (!strcmp(cmd, "status show")) {
    snprintf(reply, 160, "Tracking:%s GPS:%s channel:%u %s interval:%lu seconds", _prefs.gps_enabled ? "on" : "off",
             acquiring_position ? "acquiring" : "asleep", tracker_prefs.status_channel, tracker_prefs.status_name, (unsigned long)tracker_prefs.status_interval);
  } else if (!strcmp(cmd, "verify") || !strcmp(cmd, "status verify") || !strcmp(cmd, "status send")) {
    ChannelDetails ch;
    const char* error = verifyStatus(ch);
    if (error) snprintf(reply, 160, "BLOCKED: %s", error);
    else if (!strcmp(cmd, "status send")) { startPositionRequest(); strcpy(reply, sendStatus() ? "Status queued; acquiring until fresh fix" : "Error: status not queued"); }
    else strcpy(reply, "READY: private channel bound. GPS cycles per fix; no fix reports are labelled last known.");
  } else return false;
  return true;
}
#endif

#ifdef PRIVATE_STATUS_REPORTER
void MyMesh::logTx(mesh::Packet* packet, int len) {
  if (_listener) _listener->onPacketSent(packet);
  if (nearby_running && packet == nearby_request) {
    nearby_request = nullptr;
    nearby_deadline = millis() + 15000;
  }
#ifdef TECHO_STATUS_LEDS
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  for (auto& pending : status_led_packets) {
    if (pending.packet == packet && !memcmp(pending.hash, hash, MAX_HASH_SIZE)) {
      pending.packet = nullptr;
      board.onStatusTransmitted();
      break;
    }
  }
#endif
}
void MyMesh::logTxFail(mesh::Packet* packet, int len) {
  BaseChatMesh::logTxFail(packet, len);
  if (packet == nearby_request) {
    nearby_running = false; nearby_request = nullptr;
    trackerReply("!triangulate: discovery transmission failed; retry.");
  }
#ifdef TECHO_STATUS_LEDS
  for (auto& pending : status_led_packets) {
    if (pending.packet == packet) pending.packet = nullptr;
  }
#endif
}
#endif
