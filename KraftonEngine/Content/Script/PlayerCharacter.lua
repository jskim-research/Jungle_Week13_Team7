function BeginPlay()
    print("[BeginPlay] " .. obj.UUID)

    local comp = obj:GetPrimitiveComponent()

end

function EndPlay()
    print("[EndPlay] " .. obj.UUID)
end

function OnOverlap(OtherActor)
end

function Tick(dt)
end
