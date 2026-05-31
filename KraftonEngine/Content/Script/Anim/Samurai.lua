-- Simple Locomotion Anim Script
-- 사용:
-- 1) SkeletalMeshComponent
-- 2) Animation Mode = Custom
-- 3) Anim Instance Class = ULuaAnimInstance
-- 4) Script File = 이 lua 파일 경로

local IDLE_PATH = "Content/Animation/Samurai/SamuraiIdle.uasset"
local WALK_PATH = "Content/Animation/Samurai/SamuraiWalk.uasset"
local RUN_PATH  = "Content/Animation/Samurai/SamuraiSprint.uasset"
local JUMP_PATH = "Content/Animation/Samurai/SamuraiJump.uasset"

-- 속도 기준은 캐릭터 이동 속도에 맞춰 조정
local WALK_THRESHOLD = 5.0
local RUN_THRESHOLD  = 300.0

-- Jump 애니메이션이 제자리 점프 1회성이면 false
-- Falling Idle 같은 공중 루프 애니메이션이면 true 권장
local JUMP_LOOP = false

function init(self)
    self.Speed = 0.0

    -- ── Locomotion: Idle / Walk / Run ──
    local loco = Anim.create_state_machine("Locomotion")

    Anim.sm_add_state(loco, "Idle", Anim.create_sequence_player(IDLE_PATH, 1.0, true))
    Anim.sm_add_state(loco, "Walk", Anim.create_sequence_player(WALK_PATH, 1.0, true))
    Anim.sm_add_state(loco, "Run",  Anim.create_sequence_player(RUN_PATH,  1.0, true))

    -- Idle -> Walk / Run
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

    -- Walk -> Idle / Run
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

    -- Run -> Walk / Idle
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

    -- ── Top: Locomotion / Jump ──
    local top = Anim.create_state_machine("Top")

    Anim.sm_add_state(top, "Locomotion", loco)
    Anim.sm_add_state(top, "Jump", Anim.create_sequence_player(JUMP_PATH, 1.0, JUMP_LOOP))

    -- 지면에서 공중 상태로 바뀌면 Jump
    Anim.sm_add_transition(top, "Locomotion", "Jump",
        function()
            return Anim.is_owner_falling()
        end,
        0.1
    )

    -- 다시 착지하면 Locomotion
    Anim.sm_add_transition(top, "Jump", "Locomotion",
        function()
            return not Anim.is_owner_falling()
        end,
        0.2
    )

    Anim.sm_set_initial_state(top, "Locomotion")

    Anim.set_root_node(top)
end

function update(self, dt)
    self.Speed = Anim.get_owner_speed()
end

function on_notify(self, name)
    print("[LuaAnim] notify: " .. name)
end