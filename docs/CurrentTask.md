# CurrentTask.md - CJelly Library Development Plan

## Current State Analysis

### What's Been Built (POC Stage)
- **Basic Vulkan Framework**: Multi-window support with platform abstraction (Win32/X11)
- **Application API**: High-level Vulkan initialization with configurable constraints
- **Simple Rendering**: Two-window demo with basic colored squares and textured squares
- **Platform Abstraction**: Cross-platform window creation and event handling
- **Image Format Support**: Basic BMP loading and texture rendering

### Current Architecture Issues
1. **Verbose and Repetitive Code**: The current implementation has significant code duplication
2. **Global State Management**: Heavy reliance on global variables makes the code hard to maintain
3. **Mixed Concerns**: Platform code, Vulkan setup, and rendering logic are intertwined
4. **No Bindless Implementation**: Current shaders use traditional descriptor sets, not bindless rendering
5. **Limited Abstraction**: Direct Vulkan calls throughout the codebase

### Current Code Structure
```
src/
├── main.c              # Two-window demo with basic rendering
├── application.c       # High-level Vulkan application management
├── cjelly.c           # Low-level Vulkan framework (1700+ lines)
├── format/            # Image format support (BMP)
└── shaders/           # Basic vertex/fragment shaders
```

## Implementation Plan

### Phase 1: Bindless Rendering POC (Current Focus)
**Goal**: Convert the existing examples to use bindless rendering approach

#### Task 1.1: Descriptor Indexing Extension Setup
- [ ] **Status**: Not Started
- [ ] Add `VK_EXT_descriptor_indexing` extension to application requirements
- [ ] Implement feature detection for descriptor indexing capabilities
- [ ] Create fallback path for non-bindless hardware
- [ ] **Implementation Details**:
  - Modify `cjelly_application_add_device_extension()` calls
  - Add feature checking in device selection logic
  - Update Vulkan 1.2 requirement enforcement

#### Task 1.2: Bindless Shader Pipeline
- [ ] **Status**: Not Started  
- [ ] Create new bindless vertex shader with texture array support
- [ ] Create new bindless fragment shader using descriptor indexing
- [ ] Implement texture atlas management system
- [ ] **Implementation Details**:
  - Shader: `#version 450` with `VK_EXT_descriptor_indexing`
  - Use `layout(binding = 0) uniform texture2D textures[];`
  - Implement texture ID passing via vertex attributes or push constants

#### Task 1.3: Resource Management Refactor
- [ ] **Status**: Not Started
- [ ] Create texture atlas system for bindless rendering
- [ ] Implement dynamic texture binding without descriptor set updates
- [ ] Create resource pooling system for textures and buffers
- [ ] **Implementation Details**:
  - Single large descriptor set with all textures
  - Texture ID mapping system
  - Efficient texture upload and management

#### Task 1.4: Bindless Demo Implementation
- [ ] **Status**: Not Started
- [ ] Convert existing two-window demo to use bindless rendering
- [ ] Implement multiple textures in single draw call
- [ ] Demonstrate batching benefits of bindless approach
- [ ] **Implementation Details**:
  - Load multiple textures into atlas
  - Render different textures per window using texture IDs
  - Measure performance improvement over traditional approach

### Phase 2: Library Architecture Refactoring
**Goal**: Abstract the POC into a proper library structure

#### Task 2.1: Core API Design
- [ ] **Status**: Not Started
- [ ] Design clean C API for widget creation and management
- [ ] Implement retained-mode widget tree structure
- [ ] Create immediate-mode facade API
- [ ] **Implementation Details**:
  - `CJellyWidget` base structure
  - `CJellyRenderer` abstraction layer
  - Event system with capture/bubble model

#### Task 2.2: Rendering Backend Abstraction
- [ ] **Status**: Not Started
- [ ] Create pluggable rendering backend interface
- [ ] Implement Vulkan bindless backend
- [ ] Design CPU fallback backend interface
- [ ] **Implementation Details**:
  - `CJellyRenderBackend` interface
  - `CJellyVulkanBackend` implementation
  - `CJellyCPUBackend` stub implementation

#### Task 2.3: Scene Graph Implementation
- [ ] **Status**: Not Started
- [ ] Implement scene graph for retained-mode rendering
- [ ] Create draw list generation from scene graph
- [ ] Implement efficient dirty region tracking
- [ ] **Implementation Details**:
  - `CJellySceneNode` hierarchy
  - `CJellyDrawList` generation
  - Invalidation and update system

### Phase 3: Widget System Development
**Goal**: Implement basic widget library

#### Task 3.1: Core Widgets
- [ ] **Status**: Not Started
- [ ] Implement basic widgets: Label, Button, Image
- [ ] Create layout system (Flex, Grid, Absolute)
- [ ] Implement theming system
- [ ] **Implementation Details**:
  - Widget base class with common properties
  - Layout constraint system
  - Theme variable system

#### Task 3.2: Text Rendering
- [ ] **Status**: Not Started
- [ ] Integrate ICU for text shaping and bidi
- [ ] Implement glyph atlas system
- [ ] Create text measurement and rendering
- [ ] **Implementation Details**:
  - Font loading and management
  - Glyph caching in texture atlas
  - Text layout and measurement

### Phase 4: Advanced Features
**Goal**: Complete the v0.1 milestone

#### Task 4.1: Animation System
- [ ] **Status**: Not Started
- [ ] Implement timeline-based animation system
- [ ] Create easing functions and spring physics
- [ ] Integrate with render loop
- [ ] **Implementation Details**:
  - `CJellyAnimation` base class
  - Property animation system
  - Frame-rate independent timing

#### Task 4.2: Multi-window Management
- [ ] **Status**: Partially Complete
- [ ] Refactor current multi-window code into proper API
- [ ] Implement window lifecycle management
- [ ] Create window event system
- [ ] **Implementation Details**:
  - `CJellyWindowManager` class
  - Window creation/destruction API
  - Event routing system

## Current Blockers and Dependencies

### Immediate Blockers
1. **Bindless Extension Support**: Need to implement `VK_EXT_descriptor_indexing` support
2. **Shader Compilation**: Need to update shader compilation to support bindless features
3. **Resource Management**: Current global state needs refactoring for proper resource management

### Technical Dependencies
1. **Vulkan 1.2**: Ensure all target platforms support required features
2. **Descriptor Indexing**: Verify hardware support on target platforms
3. **Shader Toolchain**: Update SPIR-V compilation for bindless features

## Success Metrics

### Phase 1 Success Criteria
- [ ] Two-window demo runs with bindless rendering
- [ ] Multiple textures rendered in single draw call
- [ ] Performance improvement measurable over traditional approach
- [ ] Fallback path works on non-bindless hardware

### Phase 2 Success Criteria
- [ ] Clean C API for basic widget creation
- [ ] Pluggable rendering backends
- [ ] Scene graph generates correct draw lists
- [ ] No global state dependencies

### Overall v0.1 Success Criteria
- [ ] Multi-window gallery demo
- [ ] Basic widget set (Label, Button, Image, etc.)
- [ ] Text rendering with Latin support
- [ ] Light/dark theming
- [ ] Cross-platform compatibility (Windows/Linux)

## Next Immediate Steps

1. **Start with Task 1.1**: Implement descriptor indexing extension support
2. **Create bindless shader prototypes**: Design new shader pipeline
3. **Refactor resource management**: Move away from global state
4. **Implement texture atlas system**: Foundation for bindless rendering

## Notes

- Current code is in POC stage - focus on validating bindless approach before full library implementation
- The two-window demo is a good test case for bindless benefits (multiple textures, batching)
- Architecture should be designed to support both retained-mode and immediate-mode APIs
- CPU fallback is important but can be implemented after GPU path is solid
