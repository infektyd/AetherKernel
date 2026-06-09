//===----------------------------------------------------------------------===//
// Slice-1 dispatch-path probe (.internal/executor-replacement-map.md).
//
// Installs a probe executor via the @_spi ExecutorFactory route and prints a
// one-shot serial marker from its enqueue. Together with the EXPROBE-C-HOOK
// marker in executor.c this decides the executor-replacement path:
//   EXPROBE-SWIFT seen        -> factory route is live (Path A: executor.c -> 0)
//   only EXPROBE-C-HOOK seen  -> ExecutorImpl.h hooks stay the dispatch path
//                                (Path B: thin C trampolines)
// Jobs are delegated to the existing C ready ring (executor_probe_push) so the
// kernel's runtime behavior is unchanged either way.
//===----------------------------------------------------------------------===//
@_spi(ExperimentalCustomExecutors) import _Concurrency
import Support

final class ProbeExecutor: MainExecutor, TaskExecutor {
  static let shared = ProbeExecutor()
  nonisolated(unsafe) private var announced = false

  func enqueue(_ job: consuming ExecutorJob) {
    if !announced {
      announced = true
      uartPuts("EXPROBE-SWIFT enqueue via factory route\n")
    }
    let unowned = UnownedJob(job)
    executor_probe_push(unsafeBitCast(unowned, to: UnsafeMutableRawPointer.self))
  }

  // The kernel owns its run loop: main() drains via swift_task_asyncMainDrainQueue.
  func run() throws {}
  func stop() {}
}

struct ProbeFactory: ExecutorFactory {
  static var mainExecutor: any MainExecutor { ProbeExecutor.shared }
  static var defaultExecutor: any TaskExecutor { ProbeExecutor.shared }
}

// Must run after kernel_memory_init() (class allocation) and before the first
// spawnAetherTask: the runtime's lazy default-init is a no-op once
// Task._defaultExecutor is set.
func installExecutorProbe() {
  _createExecutors(factory: ProbeFactory.self)
  uartPuts("EXPROBE factory installed\n")
}
