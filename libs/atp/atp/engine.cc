#include "engine.h"
#include "isignalling.h"

namespace Atp {

Engine::Engine(ISignallingProvider* signallingProvider)
    : mSignallingProvider { signallingProvider }
    , mEventCore()
{
}

}
