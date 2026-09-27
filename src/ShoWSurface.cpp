#include "ShoWSurface.h"

namespace showthis {

ShoWSurface::ShoWSurface(QWindow *parent)
    : QRasterWindow(parent)
{}

ShoWSurface::~ShoWSurface() = default;

}
