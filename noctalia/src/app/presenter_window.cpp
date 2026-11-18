#include "app/presenter_window.h"

#include "core/ui_phase.h"
#include "render/render_context.h"
#include "ui/builders.h"

#include <stdexcept>

namespace {
  constexpr float kRadius = 12.0F;
}

tandem::PresenterWindow::PresenterWindow(
    WaylandConnection& connection, RenderContext& renderer, AnimationManager& animations, DaemonClient& daemon,
    std::string id
)
    : m_renderer(renderer), m_daemon(daemon), m_id(std::move(id)), m_surface(connection) {
  m_surface.setRenderContext(&m_renderer);
  m_surface.setAnimationManager(&animations);
  m_surface.setPrepareFrameCallback([this](bool, bool) { prepare(); });
  m_surface.setUpdateCallback([] {});
  bool fullscreen = false;
  m_surface.setConfigureCallback([this, fullscreen](std::uint32_t, std::uint32_t) mutable {
    if (!fullscreen) {
      fullscreen = true;
      m_surface.toggleFullscreen();
    }
    m_surface.setInputRegion({});
    m_surface.requestLayout();
  });
  if (!m_surface.initialize(nullptr, {.width = 640, .height = 480, .minWidth = 0, .minHeight = 0, .title = "Tandem presenter", .appId = "de.flk.Tandem.Presenter"})) {
    throw std::runtime_error("cannot create the presenter window");
  }
  m_surface.setOpaque(false);
  m_listener = m_daemon.addListener([this](const DaemonEvent& event) {
    if (event.device == m_id && event.plugin == "presenter") {
      m_surface.requestLayout();
    }
  });
  if (auto device = m_daemon.device(m_id)) {
    device->refreshPlugin("presenter");
  }
}

tandem::PresenterWindow::~PresenterWindow() { m_daemon.removeListener(m_listener); }

void tandem::PresenterWindow::prepare() {
  if (!m_surface.isRunning() || !m_renderer.makeCurrent(m_surface.renderTarget())) {
    return;
  }
  UiPhaseScope phase(UiPhase::Layout);
  if (!m_root) {
    m_root = ui::node();
    auto dot = ui::box(
        {.out = &m_dot,
         .fill = fixedColorSpec({.r = 0.93F, .g = 0.11F, .b = 0.14F, .a = 0.85F}),
         .radius = kRadius,
         .width = 2.0F * kRadius,
         .height = 2.0F * kRadius,
         .participatesInLayout = false}
    );
    m_root->addChild(std::move(dot));
    m_surface.setSceneRoot(m_root.get());
  }
  const auto device = m_daemon.device(m_id);
  const Variant state = device ? device->pluginState("presenter") : Variant();
  const bool active = state.boolean("active");
  if (active) {
    m_seenActive = true;
  } else if (m_seenActive || !device) {
    m_surface.close();
    return;
  }
  const auto width = static_cast<float>(m_surface.width()), height = static_cast<float>(m_surface.height());
  auto& renderer = m_surface.renderTarget().renderer();
  m_root->arrange(renderer, {0, 0, width, height});
  m_dot->setVisible(active);
  m_dot->arrange(
      renderer,
      {static_cast<float>(state.number("x")) * width - kRadius, static_cast<float>(state.number("y")) * height - kRadius,
       2.0F * kRadius, 2.0F * kRadius}
  );
}
