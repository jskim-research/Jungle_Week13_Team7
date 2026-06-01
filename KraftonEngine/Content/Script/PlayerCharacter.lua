local PlayerCharacter = _G.PlayerCharacter or {}
_G.PlayerCharacter = PlayerCharacter

local VK_W = string.byte("W")
local VK_A = string.byte("A")
local VK_S = string.byte("S")
local VK_D = string.byte("D")
local VK_Q = string.byte("Q")
local VK_SPACE = 0x20

local DASH_SLASH_DISTANCE = 8.0
local DASH_SLASH_DURATION = 0.2

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

local function GetOwner(ctx)
    return ctx.Owner or obj
end

local function SetMovementInputEnabled(ctx, enabled)
    if ctx.MovementComp ~= nil then
        Reflection.Call(ctx.MovementComp, "SetMovementInputEnabled", enabled)
    end
end

local function StopMovementImmediately(ctx)
    if ctx.MovementComp ~= nil then
        Reflection.Call(ctx.MovementComp, "StopMovementImmediately")
    end
end

local function SetOrientRotationToMovement(ctx, enabled)
    if ctx.MovementComp ~= nil then
        Reflection.SetProperty(ctx.MovementComp, "bOrientRotationToMovement", enabled)
    end
end

function PlayerCharacter.Initialize(ctx, owner)
    ctx.Owner = owner
    ctx.LastMoveInputDirection = nil
    ctx.DashSlashPrevOrientRotationToMovement = nil
    ctx.DashSlashMoveDirection = nil

    ctx.MovementComp = nil
    if owner ~= nil then
        Reflection.SetProperty(owner, "bUseControllerRotationYaw", false)

        if owner.GetCharacterMovement ~= nil then
            ctx.MovementComp = owner:GetCharacterMovement()
        end
        if ctx.MovementComp == nil and owner.GetFloatingPawnMovement ~= nil then
            ctx.MovementComp = owner:GetFloatingPawnMovement()
        end
    end
end

function PlayerCharacter.SetMovementInputEnabled(ctx, enabled)
    SetMovementInputEnabled(ctx, enabled)
end

function PlayerCharacter.StopMovementImmediately(ctx)
    StopMovementImmediately(ctx)
end

function PlayerCharacter.GetMoveInputWorldDirection(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
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
    local controlRot = Reflection.Call(owner, "GetControlRotation")

    if controlRot ~= nil then
        local yawRad = controlRot.Z * math.pi / 180.0
        forward = Vector(math.cos(yawRad), math.sin(yawRad), 0.0)
        right = Vector(-math.sin(yawRad), math.cos(yawRad), 0.0)
    else
        forward = Reflection.Call(owner, "GetActorForward")
        right = Reflection.Call(owner, "GetActorRight")

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

    return dir:Normalized()
end

function PlayerCharacter.GetOwnerForward2D(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
        return nil
    end

    local dir = Reflection.Call(owner, "GetActorForward")
    if dir == nil then
        return nil
    end

    dir.Z = 0.0

    if dir:Length() <= 0.001 then
        return nil
    end

    return dir:Normalized()
end

function PlayerCharacter.ResolveDashDirection(ctx)
    local dir = PlayerCharacter.GetMoveInputWorldDirection(ctx)

    if dir ~= nil then
        return dir
    end

    if ctx.LastMoveInputDirection ~= nil then
        return ctx.LastMoveInputDirection
    end

    return PlayerCharacter.GetOwnerForward2D(ctx)
end

function PlayerCharacter.FaceOwnerToDirection(ctx, dir)
    local owner = GetOwner(ctx)
    if owner == nil or dir == nil then
        return
    end

    local targetYaw = math.atan2(dir.Y, dir.X) * 180.0 / math.pi
    Reflection.Call(owner, "SetActorRotation", Vector(0.0, 0.0, targetYaw))
end

function PlayerCharacter.ApplyMoveInput(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
        return
    end

    local dir = PlayerCharacter.GetMoveInputWorldDirection(ctx)
    if dir ~= nil then
        ctx.LastMoveInputDirection = dir
        Reflection.Call(owner, "AddMovementInput", dir, 1.0)
    end

    if IsKeyPressed(VK_SPACE) then
        Reflection.Call(owner, "Jump")
    end
end

function PlayerCharacter.BeginDashSlash(ctx)
    if ctx.MovementComp ~= nil then
        ctx.DashSlashPrevOrientRotationToMovement =
            Reflection.GetProperty(ctx.MovementComp, "bOrientRotationToMovement")
        SetOrientRotationToMovement(ctx, false)
    end

    SetMovementInputEnabled(ctx, false)

    local dashDir = PlayerCharacter.ResolveDashDirection(ctx)
    PlayerCharacter.FaceOwnerToDirection(ctx, dashDir)
    ctx.DashSlashMoveDirection = dashDir

    ctx.DashSlashActive = true
    ctx.DashSlashElapsed = 0.0
    ctx.DashSlashEnd = false
end

function PlayerCharacter.EndDashSlash(ctx)
    ctx.DashSlashActive = false
    ctx.DashSlashElapsed = 0.0
    ctx.DashSlashEnd = false

    if ctx.DashSlashPrevOrientRotationToMovement ~= nil then
        SetOrientRotationToMovement(ctx, ctx.DashSlashPrevOrientRotationToMovement)
        ctx.DashSlashPrevOrientRotationToMovement = nil
    end

    ctx.DashSlashMoveDirection = nil

    SetMovementInputEnabled(ctx, true)
end

function PlayerCharacter.UpdateDashSlash(ctx, dt)
    local owner = GetOwner(ctx)
    if owner == nil or ctx.DashSlashMoveDirection == nil then
        return
    end

    ctx.DashSlashElapsed = ctx.DashSlashElapsed + dt

    local dir = ctx.DashSlashMoveDirection
    dir.Z = 0.0

    if dir:Length() > 0.001 then
        local moveSpeed = DASH_SLASH_DISTANCE / DASH_SLASH_DURATION
        Reflection.Call(owner, "AddActorWorldOffset", dir:Normalized() * moveSpeed * dt)
    end

    if ctx.DashSlashElapsed >= DASH_SLASH_DURATION then
        ctx.DashSlashEnd = true
    end
end

function PlayerCharacter.BeginUltimate()
    print("Begin Ultimate")
    PlayerCharacter.IsInUltimateMode = true
    MovementComp = obj:GetCharacterMovement()

    if MovementComp ~= nil then
        local UltimateCamera = World.FindFirstActorByTag("UltimateCamera")
        if UltimateCamera ~= nil then 
            local ActorLocation = Reflection.Call(obj, "GetActorLocation")
            local ActorForward = Reflection.Call(obj, "GetActorForward")

            local CameraStartLocation = ActorLocation + 20 * ActorForward
            Reflection.Call(UltimateCamera, "SetActorLocation", CameraStartLocation)

            CameraManager.ToggleOwnerCamera(UltimateCamera, 0)
            -- CameraManager.FadeOut(1.5)
            -- CameraManager.SetVignette(0.7, 0.7, 0.5)

            

            print("Movement found")

            Reflection.Call(MovementComp, "SetMovementInputEnabled", false)
            Wait(1.5)
            -- CameraManager.FadeIn(1.5)
            print("Wait Over")
            Reflection.Call(MovementComp, "SetMovementInputEnabled", true)        
            CameraManager.ToggleOwnerCamera(obj, 0)
        end
    else
        print("Movement not found")
    end
    
    PlayerCharacter.IsInUltimateMode = false
end

function BeginPlay()
    PlayerCharacter.IsInUltimateMode = false
    print("[BeginPlay] " .. obj.UUID)
end

function EndPlay()
    print("[EndPlay] " .. obj.UUID)
end

function OnOverlap(OtherActor)
end

function Tick(dt)
    UpdateCoroutines(dt)

    if not PlayerCharacter.IsInUltimateMode and Input.GetKeyDown(VK_Q) then
        StartCoroutine(PlayerCharacter.BeginUltimate)
    end
end

return PlayerCharacter
