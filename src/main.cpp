// Change only this name to switch the active watch face.
// Examples: health, automotive,  retro, matrix, gaming, swiss,
// aicore
// arcreactor
// automotive
// aviation
// cyberpunkradar
// falloutpipboy
// gaming
// health
// luxury
// matrix
// mechanical
// nasa
// reactor
// retro
// swiss


#define WATCH_FACE swiss

#define WATCH_FACE_STRINGIZE_DETAIL(value) #value
#define WATCH_FACE_STRINGIZE(value) WATCH_FACE_STRINGIZE_DETAIL(value)
#define WATCH_FACE_SOURCE(name) WATCH_FACE_STRINGIZE(name.cpp)

// The selected face file is guarded so it compiles only through this include.
#define WATCH_FACE_ACTIVE
#include WATCH_FACE_SOURCE(WATCH_FACE)
#undef WATCH_FACE_ACTIVE
