// use like an enum e.g. Packets::CreateEntity
namespace obf::Packets {

constexpr uint16_t Ping = 0,
	CreateEntity = 1,
	SyncEntity = 2,
	Nickname = 3,
	Controls = 4,
	AssignEntity = 5,
	DeleteEntity = 6,
	ColorEntity = 7,
	Chat = 8;

}
