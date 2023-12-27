#include <SPI.h>
#include <WiFi.h>
#include <esp32cam.h>
#include "raw_data.h"

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte) \
	((byte)&0x80 ? '1' : '0'), \
	((byte)&0x40 ? '1' : '0'), \
	((byte)&0x20 ? '1' : '0'), \
	((byte)&0x10 ? '1' : '0'), \
	((byte)&0x08 ? '1' : '0'), \
	((byte)&0x04 ? '1' : '0'), \
	((byte)&0x02 ? '1' : '0'), \
	((byte)&0x01 ? '1' : '0')

#define PACKET_CHUNK_SIZE 700

char ssid[] = "myNetwork";   //  your network SSID (name)
char pass[] = "myPassword";  // your network password
int status = WL_IDLE_STATUS;
WiFiServer server(25565);

// Minecraft Stuff
int SEGMENT_BITS = 0x7F;
int CONTINUE_BIT = 0x80;

enum {
	Handshaking = 'H',
	Status = 'S',
	Login = 'L',
	Configuration = 'C',
	Play = 'P'
} state = Handshaking;

byte CLIENT_UUID[16];
int player_entity_id = 0;

byte packet_data_buffer[5000];
byte packet_data_double_buffer[5004];
static const int BLOCK_PALLET[16] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

class Chunk {
  public:
  int x = 0;
  int y = 0;
  byte data[2048];
  byte heightmap[128];
};

class Packet {
	public:
	//byte data[5000];
	uint current_byte = 0;
	uint length = 0;
	ushort id = -1;
	Packet(WiFiClient client) {
		length = readInitialLength(client);
		for (int i = 0; i < length; i++) {
			packet_data_buffer[i] = client.read();
		}
		id = readVarInt();
	}

	Packet() {
	}

	static int readInitialLength(WiFiClient client) {
		int value = 0;
		int position = 0;
		byte currentByte;
		while (true) {
			currentByte = client.read();
			value |= (currentByte & SEGMENT_BITS) << position;
			if ((currentByte & CONTINUE_BIT) == 0) break;
			position += 7;
		}
		return value;
	}

	uint copyToBuffer(byte* buffer) {
		uint pos = 0;
		uint len = (uint)length;
		while (true) {
			if ((len & ~SEGMENT_BITS) == 0) {
				buffer[pos] = len;
				pos++;
				break;
			}
			buffer[pos] = (len & SEGMENT_BITS) | CONTINUE_BIT;
			pos++;
			len >>= 7;
		}
		for (int i = 0; i < length; i++) {
			buffer[pos] = packet_data_buffer[i];
			pos++;
		}
		return pos;
	}

	void sendToClient(WiFiClient client) {
		uint len = copyToBuffer(packet_data_double_buffer);
		//client.write((uint8_t*)packet_data_double_buffer, len);

    int chunks = len / PACKET_CHUNK_SIZE;
    for(int i = 0; i <= chunks; i++){
      if(i == chunks){
        client.write((uint8_t*)packet_data_double_buffer + (PACKET_CHUNK_SIZE * i), len - PACKET_CHUNK_SIZE*chunks);
      }else{
        client.write((uint8_t*)packet_data_double_buffer + (PACKET_CHUNK_SIZE * i), PACKET_CHUNK_SIZE);
      }
    }
	}

	byte readByte() {
		uint temp = current_byte;
		current_byte++;
		return packet_data_buffer[temp];
	}

	void writeByte(byte b) {
		packet_data_buffer[current_byte] = b;
		length++;
		current_byte++;
	}

	void readBytes(byte* bytes, uint num) {
		for (int i = 0; i < num; i++) {
			bytes[i] = readByte();
		}
	}

	void writeBytes(byte* bytes, uint num) {
		for (int i = 0; i < num; i++) {
			writeByte(bytes[i]);
		}
	}

	int readVarInt() {
		int value = 0;
		int position = 0;
		byte currentByte;
		while (true) {
			currentByte = readByte();
			value |= (currentByte & SEGMENT_BITS) << position;
			if ((currentByte & CONTINUE_BIT) == 0) break;
			position += 7;
		}
		return value;
	}

	void writeVarInt(int value) {
		uint value2 = (uint)value;
		while (true) {
			if ((value2 & ~SEGMENT_BITS) == 0) {
				writeByte(value2);
				return;
			}
			writeByte((value2 & SEGMENT_BITS) | CONTINUE_BIT);
			value2 >>= 7;
		}
	}

	long readVarLong() {
		long value = 0;
		int position = 0;
		byte currentByte;
		while (true) {
			currentByte = readByte();
			value |= (long)(currentByte & SEGMENT_BITS) << position;
		if ((currentByte & CONTINUE_BIT) == 0) break;
			position += 7;
		}
		return value;
	}

	String readString() {
		String out = "";
		int length = readVarInt();
		for (int i = 0; i < length; i++) {
			out += (char)readByte();
		}
		return out;
	}

	void writeString(String str) {
		writeVarInt(str.length());
		for (int i = 0; i < str.length(); i++) {
			writeByte((byte)str[i]);
		}
	}

	void writeShort(short value) {
		byte* point = (byte*)&value;
		byte bytes[2];
		for (int i = 0; i < 2; i++) {
			bytes[1 - i] = point[i];
		}
		for (int t = 0; t < 2; t++) {
			writeByte(bytes[t]);
		}
	}

  float readFloat(){
    float out = 0.0f;
		byte bytes[4] = { readByte(), readByte(), readByte(), readByte() };
		for (int i = 0; i < 4; i++) {
			memcpy(&out + i, &(bytes[3-i]), 1);
		}
		return out;
  }

	void writeFloat(float value){
		byte* point = (byte*)&value;
		byte bytes[4];
		for (int i = 0; i < 4; i++) {
			bytes[3 - i] = point[i];
		}
		for (int t = 0; t < 4; t++) {
			writeByte(bytes[t]);
		}
	}

	void writeDouble(double value) {
		byte* point = (byte*)&value;
		byte bytes[8];
		for (int i = 0; i < 8; i++) {
			bytes[7 - i] = point[i];
		}
		for (int t = 0; t < 8; t++) {
			writeByte(bytes[t]);
		}
	}

	ushort readUShort() {
		byte b1 = readByte();
		byte b2 = readByte();
		ushort out = 0;
		out = (b1 << 8) | b2;
		return out;
	}

	void writeInt(int value){
		byte* point = (byte*)&value;
		byte bytes[4];
		for (int i = 0; i < 4; i++) {
			bytes[3 - i] = point[i];
		}
		for (int t = 0; t < 4; t++) {
			writeByte(bytes[t]);
		}
	}

	long readLong() {
		long out = 0;
		byte bytes[8] = { readByte(), readByte(), readByte(), readByte(), readByte(), readByte(), readByte(), readByte() };
		for (int i = 0; i < 8; i++) {
			out = out | (bytes[7 - i] << i * 8);
		}
		return out;
	}

	void writeLong(long value) {
		byte* point = (byte*)&value;
		byte bytes[8];
		for (int i = 0; i < 8; i++) {
			bytes[7 - i] = point[i];
		}
		for (int t = 0; t < 8; t++) {
			writeByte(bytes[t]);
		}
	}

	bool readBool() {
		byte b = readByte();
		return b == 0x01;
	}

	void writeBool(bool value){
		writeByte((byte)value);
	}
};

void printHexString(String str) {
	for (int i = 0; i < str.length(); i++) {
		Serial.print("\\x");
		if ((byte)str[i] <= 15) {
			Serial.print("0");
		}
		Serial.print(str[i], HEX);
	}
	Serial.println("");
}

void printHexBytes(byte* data, int len) {
	for (int i = 0; i < len; i++) {
		Serial.print("\\x");
		if (data[i] <= 15) {
			Serial.print("0");
		}
		Serial.print(data[i], HEX);
	}
	Serial.println("");
}

String bytesToUUID(byte* bytes) {
	char uuid_buffer[32];
	String out = "";
	for (int i = 0; i < 16; i++) {
		byte b = bytes[i];
		sprintf(&(uuid_buffer[i * 2]), "%x", b & 0xff);
	}
	for (int i = 0; i < 32; i++) {
		out += uuid_buffer[i];
	}
	return out;
}

void sendRawPacket(WiFiClient client, byte* packet, unsigned int len){
  int chunks = len / PACKET_CHUNK_SIZE;
  for(int i = 0; i <= chunks; i++){
    if(i == chunks){
      client.write((uint8_t*)packet + (PACKET_CHUNK_SIZE * i), len - PACKET_CHUNK_SIZE*chunks);
    }else{
      client.write((uint8_t*)packet + (PACKET_CHUNK_SIZE * i), PACKET_CHUNK_SIZE);
    }
  }
}

void sendBrandPluginMessage(WiFiClient client){
  Packet packet_out;
	packet_out.writeVarInt(0x00);
	packet_out.writeString("minecraft:brand");
	packet_out.writeString("vanilla");
	packet_out.sendToClient(client);
}

void sendFeatureFlags(WiFiClient client){
  Packet packet_out;
	packet_out.writeVarInt(0x08);
	packet_out.writeVarInt(1);
	packet_out.writeString("minecraft:vanilla");
	packet_out.sendToClient(client);
}

void sendFinishConfiguration(WiFiClient client){
  Packet packet_out;
	packet_out.writeVarInt(0x02);
	packet_out.sendToClient(client);
}

void sendTags(WiFiClient client){
  Packet packet_out;
	packet_out.writeVarInt(0x09);
	packet_out.writeVarInt(5);
  packet_out.writeString("minecraft:block");
  packet_out.writeVarInt(0);
  packet_out.writeString("minecraft:item");
  packet_out.writeVarInt(0);
  packet_out.writeString("minecraft:fluid");
  packet_out.writeVarInt(0);
  packet_out.writeString("minecraft:entity_type");
  packet_out.writeVarInt(0);
  packet_out.writeString("minecraft:game_event");
  packet_out.writeVarInt(0);
	packet_out.sendToClient(client);
}

void sendRegistry(WiFiClient client){
  Packet packet_out;
  packet_out.writeVarInt(0x05);
  packet_out.writeBytes((byte*)registry_packet, registry_packet_len);

  packet_out.sendToClient(client);
}

void sendLoginPlay(WiFiClient client){
	Packet packet_out;
	packet_out.writeVarInt(0x29);
	packet_out.writeInt(player_entity_id);
	packet_out.writeBool(false);
	packet_out.writeVarInt(1);
	packet_out.writeString("minecraft:overworld");
	packet_out.writeVarInt(100);
	packet_out.writeVarInt(10);
	packet_out.writeVarInt(5);
	packet_out.writeBool(false);
	packet_out.writeBool(true);
	packet_out.writeBool(false);
	packet_out.writeString("minecraft:overworld");
	packet_out.writeString("minecraft:overworld");
	packet_out.writeLong((long)0);
	packet_out.writeByte(0x02);
	packet_out.writeByte(0x03);
	packet_out.writeBool(false);
	packet_out.writeBool(true);
	packet_out.writeBool(false);
	packet_out.writeVarInt(20);
	packet_out.sendToClient(client);
}

void sendDifficultyChange(WiFiClient client, byte difficulty){
  // 0x00: peaceful, 0x01: easy, 0x02: normal, 0x03: hard
	Packet packet_out;
	packet_out.writeVarInt(0x0b);
	packet_out.writeByte(0x00);
	packet_out.writeBool(true);
	packet_out.sendToClient(client);
}

void sendChangePlayerAbilities(WiFiClient client){
	Packet packet_out;
	packet_out.writeVarInt(0x36);
	packet_out.writeByte(0x00);
	packet_out.writeFloat(0.05f);
	packet_out.writeFloat(0.1f);
	packet_out.sendToClient(client);
}

void sendSetPlayerSlot(WiFiClient client, byte slot){
	Packet packet_out;
	packet_out.writeVarInt(0x51);
	packet_out.writeByte(slot);
	packet_out.sendToClient(client);
}

void sendCommandList(WiFiClient client){
	Packet packet_out;
	packet_out.writeVarInt(0x11);
	packet_out.writeVarInt(0);
	packet_out.writeVarInt(0);
	packet_out.sendToClient(client);
}

void sendSyncPlayerPosition(WiFiClient client, double x, double y, double z){
	Packet packet_out;
	packet_out.writeVarInt(0x3E);
	packet_out.writeDouble(x);
	packet_out.writeDouble(y);
	packet_out.writeDouble(z);
	packet_out.writeFloat(0.0f);
	packet_out.writeFloat(0.0f);
	packet_out.writeByte(0x1F);
	packet_out.writeVarInt(69420);
	packet_out.sendToClient(client);
}

void sendServerData(WiFiClient client){
	Packet packet_out;
	packet_out.writeVarInt(0x49);
	packet_out.writeVarInt(0x49);
	packet_out.sendToClient(client);
}

void sendStartChunkBatch(WiFiClient client){
  Packet packet_out;
	packet_out.writeVarInt(0x0D);
	packet_out.sendToClient(client);
}

void sendEndChunkBatch(WiFiClient client, int num_chunks){
  Packet packet_out;
	packet_out.writeVarInt(0x0C);
  packet_out.writeVarInt(num_chunks);
	packet_out.sendToClient(client);
}

void sendChunkData(WiFiClient client, int x, int y){
  sendRawPacket(client, (byte*)chunk_packet, chunk_packet_len);
}

void sendBundleDelimiter(WiFiClient client) {
	Packet packet_out;
	packet_out.writeVarInt(0x00);
	packet_out.sendToClient(client);
}

void sendDisconnectClient(WiFiClient client, String reason) {
	Packet packet_out;
	packet_out.writeVarInt(0x1B);
	String message = "{\"text\":\"" + reason + "\"}";
	packet_out.writeString(message);
	packet_out.sendToClient(client);
}

void sendKeepAlive(WiFiClient client){
  Packet packet_out;
	packet_out.writeVarInt(0x24);
  packet_out.writeLong((long)420);
  packet_out.sendToClient(client);
}

void sendInventory(WiFiClient client){
  Packet packet_out;
	packet_out.writeVarInt(0x13);
  packet_out.writeBytes((byte*)inventory_data, inventory_data_len);
  packet_out.sendToClient(client);
}

byte getClosestColor(byte r, byte g, byte b){
  int least_dist = 255*3;
  int id = 0;
  for(int i = 1; i < 62; i++){
    int dist = abs(map_colors[i*3] - r) + abs(map_colors[i*3+1] - g) + abs(map_colors[i*3+2] - b);
    if(dist < least_dist){
      least_dist = dist;
      id = i;
    }
  }
  return id*4+2;
}

void sendMapUpdate(WiFiClient client, int map_id){
  Packet packet_out;
  packet_out.writeVarInt(16396); //length

	packet_out.writeVarInt(0x2A);
  packet_out.writeVarInt(map_id);
  packet_out.writeByte(0x00);
  packet_out.writeBool(true);
  packet_out.writeBool(false);
  packet_out.writeByte(0x80);
  packet_out.writeByte(0x80);
  packet_out.writeByte(0x00);
  packet_out.writeByte(0x00);
  packet_out.writeVarInt(128*128);

  client.write((uint8_t*)packet_data_buffer, packet_out.length);

  auto frame = esp32cam::capture();
  if (frame == nullptr) {
    Serial.println("Capture Failure");
    return;
  }
  frame->toBmp();
  uint8_t* frame_data = frame->data()+56;
  printf("(%d, %d, %d) Size: (%d, %d) Length: %d \n", frame_data[0], frame_data[1], frame_data[2], frame->getWidth(), frame->getHeight(), frame->size());

  byte row_buffer[PACKET_CHUNK_SIZE];
  int buffer_index = 0;

  int frame_width = frame->getWidth();
  int frame_height = frame->getHeight();

  for(int i = 0; i < 16384; i++){
    buffer_index = i % PACKET_CHUNK_SIZE;
    int x = i%128 + (frame_width - 128)/2;
    int y = i/128 + (frame_height - 128)/2;
    int pixel_index = (y*frame_width+x)*3;
    row_buffer[buffer_index] = getClosestColor(frame_data[pixel_index], frame_data[pixel_index+1], frame_data[pixel_index+2]);

    if(buffer_index == PACKET_CHUNK_SIZE-1){
      client.write((uint8_t*)row_buffer, PACKET_CHUNK_SIZE);
    }
  }
  client.write((uint8_t*)row_buffer, buffer_index+1);
}

void setup() {
  Serial.begin(115200);
  {
    using namespace esp32cam;
    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(Resolution::find(128, 128));

    bool ok = Camera.begin(cfg);
    if (!ok) {
      Serial.println("camera initialize failure");
      delay(5000);
      ESP.restart();
    }
    Serial.println("camera initialize success");
  }

	Serial.print("SSID: ");
	Serial.println(ssid);
	Serial.print("Password: ");
	Serial.println(pass);

	WiFi.softAP(ssid, pass);
	IPAddress IP = WiFi.softAPIP();
	Serial.print("IP address: ");
	Serial.println(IP);

	server.begin();
}

void loop() {
	WiFiClient client = server.available();

	if (client) {
		client.setNoDelay(true);
		while (client.connected()) {
			while (client.available() > 0) {
				Packet packet(client);
				//Serial.printf("\nPACKET | State: %c | Length: %d | ID: %d ------------------------------------------\n", state, packet.length, packet.id);
				//if (packet.length > 1 && packet.length < 100) printHexBytes(packet_data_buffer, packet.length);

				switch (state) {
					case Handshaking:
					{
						int version = packet.readVarInt();
						String addr = packet.readString();
						ushort port = packet.readUShort();
						int next_state = packet.readVarInt();
						Serial.printf("Version: %d\nAddr: %s\nPort: %d\nNext State: %d\n", version, addr, port, next_state);

						if (next_state == 1) {
							state = Status;
						} else if (next_state == 2) {
							state = Login;
						}
					}
						break;

					case Status:
					{
						if (packet.id == 0x00) {
							Serial.println("Status Request");

							String status = "{\"version\":{\"name\":\"1.20.4\",\"protocol\":765},\"enforcesSecureChat\":true,\"description\":\"A Minecraft Server (that's also a camera)\",\"players\":{\"max\":100,\"online\":69}}";

							Packet packet_out;
							packet_out.writeVarInt(0x00);
							packet_out.writeString(status);

							packet_out.sendToClient(client);
						} else if (packet.id == 0x01) {
							long ping = packet.readLong();
							Serial.printf("Ping request: %d\n", ping);

							Packet packet_out;
							packet_out.writeVarInt(0x01);
							packet_out.writeLong(ping);

							packet_out.sendToClient(client);
						}
					}
					break;

					case Login:
					{
						if (packet.id == 0x00) {
							String player_name = packet.readString();
							packet.readBytes(CLIENT_UUID, 16);
							Serial.printf("Username: %s\nUUID: ", player_name);
							printHexBytes(CLIENT_UUID, 16);

							Packet packet_out;
							//packet_out.writeVarInt(0);
							//packet_out.writeString("{\"text\":\"fuck you\"}");
							packet_out.writeVarInt(0x02);
							packet_out.writeBytes(CLIENT_UUID, 16);
							packet_out.writeString(player_name);
							packet_out.writeVarInt(0);

							packet_out.sendToClient(client);
						} else if (packet.id == 0x03) {
							Serial.println("Login Acknowledged");

							state = Configuration;
							Serial.println("\nStarting Configuration");

              sendBrandPluginMessage(client);
              sendFeatureFlags(client);
              sendRegistry(client);
              sendTags(client);
							sendFinishConfiguration(client);
						}
					}
					break;

					case Configuration:
					{
						if (packet.id == 0x00) {
							String locale = packet.readString();
							byte view_distance = packet.readByte();
							int chat_mode = packet.readVarInt();
							bool chat_colors = packet.readBool();
							byte displayed_skin_parts = packet.readByte();
							int main_hand = packet.readVarInt();
							bool text_filtering_enabled = packet.readBool();
							bool allow_server_listings = packet.readBool();

							Serial.printf("Locale: %s \nView Distance: %d \nChat Mode: %d \nChat Colors: %d \nDisplayed Skin: " BYTE_TO_BINARY_PATTERN " \nMain Hand: %d \nText Filtering: %d \nAllow Server Listings: %d\n", locale, view_distance, chat_mode, chat_colors, BYTE_TO_BINARY(displayed_skin_parts), main_hand, text_filtering_enabled, allow_server_listings);
						} else if (packet.id == 0x01) {
							String channel = packet.readString();
							String message = packet.readString();

							Serial.print("Plugin message sent to channel: ");
							Serial.println(channel);
							Serial.print("Message: ");
							Serial.println(message);
						} else if (packet.id == 0x02) {
							Serial.println("Finished Configuration");

							state = Play;
              sendLoginPlay(client);
              sendDifficultyChange(client, 0x00);
              sendChangePlayerAbilities(client);
              sendSetPlayerSlot(client, 0x01);
              sendCommandList(client);
              sendSyncPlayerPosition(client, 0.0d, 100.0d, 0.0d);
              sendInventory(client);
              sendMapUpdate(client, 0);

              sendStartChunkBatch(client);
              sendChunkData(client, 0, 0);
              sendEndChunkBatch(client, 0);
						}
					}
					break;

					case Play:
					{
            if(packet.id == 0x07){
              float chunks_per_second = packet.readFloat();
              Serial.printf("Chunk Batch Recieved\nDesired CPS: %f \n", chunks_per_second);
            }
					}
					break;
				}
			}
      if(state == Play){
        sendKeepAlive(client);
        sendMapUpdate(client, 0);
        //sendCameraUpdate(client);
        //delay(100);
      }
		}
		client.stop();
		state = Handshaking;
	}
}