#pragma once

namespace app::bootstrap::runtime {

class RuntimeComposition;

void bind_runtime_composition(RuntimeComposition* runtime);
RuntimeComposition* current_runtime_composition();

} // namespace app::bootstrap::runtime
