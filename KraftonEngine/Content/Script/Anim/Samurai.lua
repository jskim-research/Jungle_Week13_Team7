-- Simple Locomotion + 5 Combo Attack + DashSlash Anim Script

local IDLE_PATH = "Content/Animation/Samurai_UE4/SamuraiIdle.uasset"
local WALK_PATH = "Content/Animation/Samurai_UE4/SamuraiWalk.uasset"
local RUN_PATH  = "Content/Animation/Samurai_UE4/SamuraiSprint.uasset"
local JUMP_PATH = "Content/Animation/Samurai_UE4/SamuraiJump.uasset"

local ATTACK1_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack1.uasset"
local ATTACK2_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack2.uasset"
local ATTACK3_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack3.uasset"
local ATTACK4_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack4.uasset"
local ATTACK5_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack5.uasset"

local DASH_SLASH_PATH = "Content/Animation/Samurai_UE4/SamuraiAttackHeavy1_Start.uasset"

local WALK_THRESHOLD = 0.1
local RUN_THRESHOLD  = 8.0
local JUMP_LOOP = false

local ATTACK_BLEND_IN  = 0.08
local ATTACK_BLEND_OUT = 0.15

local DASH_SLASH_BLEND_IN  = 0.05
local DASH_SLASH_BLEND_OUT = 0.12

local DASH_SLASH_DISTANCE = 8.0
local DASH_SLASH_DURATION = 0.2

local VK_SHIFT = 0x10
local VK_W = string.byte("W")
local VK_A = string.byte("A")
local VK_S = string.byte("S")
local VK_D = string.byte("D")
local VK_SPACE = 0x20

local function IsKeyDown(vk)
    if Input ~= nil and Input.GetKey ~= nil then
        return Input.GetKey(vk)
    end

    if Input ~= nil and Input.is_key_down ~= nil then
        return Input.is_key_down(vk)
    end

    if Anim ~= nil and Anim.is_key_down ~= nil then
        return Anim.is_key_down(vk)
    end

    if Anim ~= nil and Anim.is_key_pressed ~= nil then
        return Anim.is_key_pressed(vk)
    end

    return false
end

local function IsKeyPressed(vk)
    if Input ~= nil and Input.GetKeyDown ~= nil then
        return Input.GetKeyDown(vk)
    end

    if Input ~= nil and Input.is_key_pressed ~= nil then
        return Input.is_key_pressed(vk)
    end

    if Anim ~= nil and Anim.is_key_pressed ~= nil then
        return Anim.is_key_pressed(vk)
    end

    return false
end

local function SetMovementInputEnabled(self, enabled)
    if self.MovementComp ~= nil then
        Reflection.Call(self.MovementComp, "SetMovementInputEnabled", enabled)
    end
end

local function StopMovementImmediately(self)
    if self.MovementComp ~= nil then
        Reflection.Call(self.MovementComp, "StopMovementImmediately")
    end
end

local function SetOrientRotationToMovement(self, enabled)
    if self.MovementComp ~= nil then
        Reflection.SetProperty(self.MovementComp, "bOrientRotationToMovement", enabled)
    end
end

local function GetMoveInputWorldDirection()
    if obj == nil then
        return nil
    end

    local moveForward = 0.0
    local moveRight = 0.0

    if IsKeyDown(VK_W) then moveForward = moveForward + 1.0 end
    if IsKeyDown(VK_S) then moveForward = moveForward - 1.0 end
    if IsKeyDown(VK_D) then moveRight = moveRight + 1.0 end
    if IsKeyDown(VK_A) then moveRight = moveRight - 1.0 end

    if math.abs(moveForward) < 0.001 and math.abs(moveRight) < 0.001 then
        return nil
    end

    local forward = nil
    local right = nil
    local controlRot = Reflection.Call(obj, "GetControlRotation")

    if controlRot ~= nil then
        local yawRad = controlRot.Z * math.pi / 180.0
        forward = Vector(math.cos(yawRad), math.sin(yawRad), 0.0)
        right = Vector(-math.sin(yawRad), math.cos(yawRad), 0.0)
    else
        forward = Reflection.Call(obj, "GetActorForward")
        right = Reflection.Call(obj, "GetActorRight")

        if forward == nil or right == nil then
            return nil
        end

        forward.Z = 0.0
        right.Z = 0.0
    end

    local dir = forward * moveForward + right * moveRight
    dir.Z = 0.0

    if dir:Length() <= 0.001 then
        return nil
    end

    dir = dir:Normalized()

    return dir
end

-- Z 축 성분을 제거하기에 2D 라는 이름이 붙음
local function GetOwnerForward2D()
    if obj == nil then
        return nil
    end

    local dir = Reflection.Call(obj, "GetActorForward")
    if dir == nil then
        return nil
    end

    dir.Z = 0.0

    if dir:Length() <= 0.001 then
        return nil
    end

    return dir:Normalized()
end

local function ResolveDashDirection(self)
    local dir = GetMoveInputWorldDirection()

    if dir ~= nil then
        return dir
    end

    if self.LastMoveInputDirection ~= nil then
        return self.LastMoveInputDirection
    end

    return GetOwnerForward2D()
end

local function FaceOwnerToDirection(dir)
    if dir == nil then
        return
    end

    local targetYaw = math.atan2(dir.Y, dir.X) * 180.0 / math.pi
    Reflection.Call(obj, "SetActorRotation", Vector(0.0, 0.0, targetYaw))
end

local function ApplyOwnerMoveInput(self)
    if obj == nil then
        return
    end

    local dir = GetMoveInputWorldDirection()
    if dir ~= nil then
        self.LastMoveInputDirection = dir
        Reflection.Call(obj, "AddMovementInput", dir, 1.0)
    end

    if IsKeyPressed(VK_SPACE) then
        Reflection.Call(obj, "Jump")
    end
end

local function ResetAttack(self, unlockMovement)
    self.AttackIndex = 0
    self.ComboWindow = false
    self.ComboQueued = false
    self.AttackEnd = false

    if unlockMovement ~= false then
        SetMovementInputEnabled(self, true)
    end
end

local function BeginAttack(self, index)
    self.AttackIndex = index
    self.ComboWindow = false
    self.ComboQueued = false
    self.AttackEnd = false
end

local function BeginDashSlash(self)
    ResetAttack(self, false)

    if self.MovementComp ~= nil then
        self.DashSlashPrevOrientRotationToMovement =
            Reflection.GetProperty(self.MovementComp, "bOrientRotationToMovement")
        SetOrientRotationToMovement(self, false)
    end

    SetMovementInputEnabled(self, false)

    local dashDir = ResolveDashDirection(self)
    FaceOwnerToDirection(dashDir)
    self.DashSlashMoveDirection = dashDir

    self.DashSlashActive = true
    self.DashSlashElapsed = 0.0
    self.DashSlashEnd = false
end

local function EndDashSlash(self)
    self.DashSlashActive = false
    self.DashSlashElapsed = 0.0
    self.DashSlashEnd = false

    if self.DashSlashPrevOrientRotationToMovement ~= nil then
        SetOrientRotationToMovement(self, self.DashSlashPrevOrientRotationToMovement)
        self.DashSlashPrevOrientRotationToMovement = nil
    end

    self.DashSlashMoveDirection = nil

    SetMovementInputEnabled(self, true)
end

local function MoveOwnerForwardOnDash(self, distance)
    if obj == nil then
        return
    end

    local forward = self.DashSlashMoveDirection
    if forward == nil then
        return
    end

    forward.Z = 0.0

    if forward:Length() <= 0.001 then
        return
    end

    forward = forward:Normalized()

    local delta = forward * distance

    -- AActor::AddActorWorldOffset 이 UFUNCTION 으로 노출되어 있어야 함
    Reflection.Call(obj, "AddActorWorldOffset", delta)
end

function init(self)
    self.Speed = 0.0

    self.AttackPressed = false
    self.DashSlashPressed = false

    self.DashSlashActive = false
    self.DashSlashElapsed = 0.0
    self.DashSlashEnd = false
    self.DashSlashPrevOrientRotationToMovement = nil
    self.DashSlashMoveDirection = nil
    self.LastMoveInputDirection = nil

    self.MovementComp = nil
    if obj ~= nil then
        Reflection.SetProperty(obj, "bUseControllerRotationYaw", false)

        if obj.GetCharacterMovement ~= nil then
            self.MovementComp = obj:GetCharacterMovement()
        end
        if self.MovementComp == nil and obj.GetFloatingPawnMovement ~= nil then
            self.MovementComp = obj:GetFloatingPawnMovement()
        end
    end

    ResetAttack(self)

    local loco = Anim.create_state_machine("Locomotion")

    Anim.sm_add_state(loco, "Idle", Anim.create_sequence_player(IDLE_PATH, 1.0, true))
    Anim.sm_add_state(loco, "Walk", Anim.create_sequence_player(WALK_PATH, 1.0, true))
    Anim.sm_add_state(loco, "Run",  Anim.create_sequence_player(RUN_PATH,  1.0, true))

    Anim.sm_add_transition(loco, "Idle", "Walk",
        function()
            return self.Speed > WALK_THRESHOLD and self.Speed < RUN_THRESHOLD
        end,
        0.2
    )

    Anim.sm_add_transition(loco, "Idle", "Run",
        function()
            return self.Speed >= RUN_THRESHOLD
        end,
        0.2
    )

    Anim.sm_add_transition(loco, "Walk", "Idle",
        function()
            return self.Speed <= WALK_THRESHOLD
        end,
        0.2
    )

    Anim.sm_add_transition(loco, "Walk", "Run",
        function()
            return self.Speed >= RUN_THRESHOLD
        end,
        0.2
    )

    Anim.sm_add_transition(loco, "Run", "Walk",
        function()
            return self.Speed > WALK_THRESHOLD and self.Speed < RUN_THRESHOLD
        end,
        0.2
    )

    Anim.sm_add_transition(loco, "Run", "Idle",
        function()
            return self.Speed <= WALK_THRESHOLD
        end,
        0.2
    )

    Anim.sm_set_initial_state(loco, "Idle")

    local top = Anim.create_state_machine("Top")

    Anim.sm_add_state(top, "Locomotion", loco)
    Anim.sm_add_state(top, "Jump", Anim.create_sequence_player(JUMP_PATH, 1.0, JUMP_LOOP))

    Anim.sm_add_state(top, "Attack1", Anim.create_sequence_player(ATTACK1_PATH, 1.0, false))
    Anim.sm_add_state(top, "Attack2", Anim.create_sequence_player(ATTACK2_PATH, 1.0, false))
    Anim.sm_add_state(top, "Attack3", Anim.create_sequence_player(ATTACK3_PATH, 1.0, false))
    Anim.sm_add_state(top, "Attack4", Anim.create_sequence_player(ATTACK4_PATH, 1.0, false))
    Anim.sm_add_state(top, "Attack5", Anim.create_sequence_player(ATTACK5_PATH, 1.0, false))

    Anim.sm_add_state(top, "DashSlash", Anim.create_sequence_player(DASH_SLASH_PATH, 3.0, false))

    -- AnyState → DashSlash
    Anim.sm_add_transition(top, "AnyState", "DashSlash",
        function()
            if self.DashSlashPressed and not self.DashSlashActive and not Anim.is_owner_falling() then
                BeginDashSlash(self)
                return true
            end
            return false
        end,
        DASH_SLASH_BLEND_IN
    )

    -- DashSlash → Locomotion
    Anim.sm_add_transition(top, "DashSlash", "Locomotion",
        function()
            if self.DashSlashEnd then
                EndDashSlash(self)
                return true
            end
            return false
        end,
        DASH_SLASH_BLEND_OUT
    )

    -- Locomotion → Jump
    Anim.sm_add_transition(top, "Locomotion", "Jump",
        function()
            return Anim.is_owner_falling()
        end,
        0.1
    )

    -- Jump → Locomotion
    Anim.sm_add_transition(top, "Jump", "Locomotion",
        function()
            return not Anim.is_owner_falling()
        end,
        0.2
    )

    -- Locomotion → Attack1
    Anim.sm_add_transition(top, "Locomotion", "Attack1",
        function()
            if self.AttackPressed then
                BeginAttack(self, 1)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    -- Attack1 → Attack2 or Locomotion
    Anim.sm_add_transition(top, "Attack1", "Attack2",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 2)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack1", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    -- Attack2 → Attack3 or Locomotion
    Anim.sm_add_transition(top, "Attack2", "Attack3",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 3)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack2", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    -- Attack3 → Attack4 or Locomotion
    Anim.sm_add_transition(top, "Attack3", "Attack4",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 4)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack3", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    -- Attack4 → Attack5 or Locomotion
    Anim.sm_add_transition(top, "Attack4", "Attack5",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 5)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack4", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    -- Attack5 → Locomotion
    Anim.sm_add_transition(top, "Attack5", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    Anim.sm_set_initial_state(top, "Locomotion")

    local root = Anim.create_slot("DefaultSlot", top)
    Anim.set_root_node(root)
end

function update(self, dt)
    self.Speed = Anim.get_owner_speed()

    self.AttackPressed = Anim.is_left_mouse_pressed()
    self.DashSlashPressed = Anim.is_key_pressed(VK_SHIFT)

    -- 공격 중 ComboWindow 안에서 클릭하면 다음 공격 예약
    if self.AttackPressed and self.AttackIndex > 0 and self.ComboWindow then
        self.ComboQueued = true
    end

    -- DashSlash 중 전방 이동
    if self.DashSlashActive then
        self.DashSlashElapsed = self.DashSlashElapsed + dt

        local moveSpeed = DASH_SLASH_DISTANCE / DASH_SLASH_DURATION
        MoveOwnerForwardOnDash(self, moveSpeed * dt)

        if self.DashSlashElapsed >= DASH_SLASH_DURATION then
            self.DashSlashEnd = true
        end
    elseif self.AttackIndex == 0 then
        ApplyOwnerMoveInput(self)
    end
end

function on_notify(self, name)
    print("[LuaAnim] notify: " .. name)

    if name == "ComboWindowOpen" then
        self.ComboWindow = true
        return
    end

    if name == "ComboWindowClose" then
        self.ComboWindow = false
        return
    end

    if name == "AttackEnd" then
        self.ComboWindow = false
        self.AttackEnd = true
        return
    end

    if name == "DashSlashEnd" then
        self.DashSlashEnd = true
        return
    end
end
