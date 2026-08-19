param(
    [Parameter(Mandatory = $true)]
    [string]$PhoenixCaptureDir,

    [Parameter(Mandatory = $true)]
    [string]$TioOutputDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ObjSummary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $vertices = @()
    $faceCount = 0
    Get-Content -Path $Path | ForEach-Object {
        if ($_ -like 'v *') {
            $parts = $_ -split ' '
            $vertices += [pscustomobject]@{
                x = [double]$parts[1]
                y = [double]$parts[2]
                z = [double]$parts[3]
            }
        } elseif ($_ -like 'f *') {
            $faceCount++
        }
    }

    if ($vertices.Count -eq 0) {
        return [pscustomobject]@{
            faceCount = $faceCount
            vertexCount = 0
            minX = $null
            maxX = $null
            minY = $null
            maxY = $null
            minZ = $null
            maxZ = $null
        }
    }

    return [pscustomobject]@{
        faceCount = $faceCount
        vertexCount = $vertices.Count
        minX = ($vertices.x | Measure-Object -Minimum).Minimum
        maxX = ($vertices.x | Measure-Object -Maximum).Maximum
        minY = ($vertices.y | Measure-Object -Minimum).Minimum
        maxY = ($vertices.y | Measure-Object -Maximum).Maximum
        minZ = ($vertices.z | Measure-Object -Minimum).Minimum
        maxZ = ($vertices.z | Measure-Object -Maximum).Maximum
    }
}

function Get-TioMeshSummary {
    param(
        [Parameter(Mandatory = $true)]
        $Mesh
    )

    $vertices = @()
    if ($Mesh.PSObject.Properties.Name -contains 'vertices') {
        $triples = @($Mesh.vertices)
        for ($i = 0; $i -lt $triples.Count; $i += 3) {
            $vertices += [pscustomobject]@{
                x = [double]$triples[$i]
                y = [double]$triples[$i + 1]
                z = [double]$triples[$i + 2]
            }
        }
    } else {
        $xs = @($Mesh.vertices_x)
        $ys = @($Mesh.vertices_y)
        $zs = @($Mesh.vertices_z)
        $count = [Math]::Max($xs.Count, [Math]::Max($ys.Count, $zs.Count))
        for ($i = 0; $i -lt $count; ++$i) {
            $vertices += [pscustomobject]@{
                x = if ($i -lt $xs.Count) { [double]$xs[$i] } else { [double]$xs[-1] }
                y = if ($i -lt $ys.Count) { [double]$ys[$i] } else { [double]$ys[-1] }
                z = if ($i -lt $zs.Count) { [double]$zs[$i] } else { [double]$zs[-1] }
            }
        }
    }

    return [pscustomobject]@{
        faceCount = @($Mesh.faceids).Count
        vertexCount = $vertices.Count
        minX = ($vertices.x | Measure-Object -Minimum).Minimum
        maxX = ($vertices.x | Measure-Object -Maximum).Maximum
        minY = ($vertices.y | Measure-Object -Minimum).Minimum
        maxY = ($vertices.y | Measure-Object -Maximum).Maximum
        minZ = ($vertices.z | Measure-Object -Minimum).Minimum
        maxZ = ($vertices.z | Measure-Object -Maximum).Maximum
    }
}

function Get-TioSnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return ((Get-Content -Raw -Path $Path).TrimEnd('&') | ConvertFrom-Json)
}

function Compare-Stage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        $Phoenix,

        [Parameter(Mandatory = $true)]
        $Tio
    )

    $zMirrorAligned = $false
    if ($null -ne $Phoenix.minZ -and $null -ne $Tio.minZ) {
        $zMirrorAligned =
            ([Math]::Abs([double]$Phoenix.minZ + [double]$Tio.maxZ) -lt 0.05) -and
            ([Math]::Abs([double]$Phoenix.maxZ + [double]$Tio.minZ) -lt 0.05)
    }

    return [pscustomobject]@{
        stage = $Name
        phoenixFaces = $Phoenix.faceCount
        tioFaces = $Tio.faceCount
        phoenixVertices = $Phoenix.vertexCount
        tioVertices = $Tio.vertexCount
        phoenixBounds = ('x=[{0},{1}] y=[{2},{3}] z=[{4},{5}]' -f
            $Phoenix.minX, $Phoenix.maxX, $Phoenix.minY, $Phoenix.maxY, $Phoenix.minZ, $Phoenix.maxZ)
        tioBounds = ('x=[{0},{1}] y=[{2},{3}] z=[{4},{5}]' -f
            $Tio.minX, $Tio.maxX, $Tio.minY, $Tio.maxY, $Tio.minZ, $Tio.maxZ)
        zMirrorAligned = $zMirrorAligned
        faceCountMatch = ($Phoenix.faceCount -eq $Tio.faceCount)
    }
}

$partitionSnapshot = Get-TioSnapshot -Path (Join-Path $TioOutputDir '2_2.json')
$partitionMeshSummaries = @($partitionSnapshot.meshes | ForEach-Object { Get-TioMeshSummary $_ })
$partitionVertexCounts = @($partitionSnapshot.meshes | ForEach-Object {
    if ($_.PSObject.Properties.Name -contains 'vertices') { @($_.vertices).Count / 3 }
    else { [Math]::Max(@($_.vertices_x).Count, [Math]::Max(@($_.vertices_y).Count, @($_.vertices_z).Count)) }
})
$partitionSummary = [pscustomobject]@{
    faceCount = [int]$partitionSnapshot.facesCount
    vertexCount = ($partitionVertexCounts | Measure-Object -Sum).Sum
    minX = (($partitionMeshSummaries | Select-Object -ExpandProperty minX) | Measure-Object -Minimum).Minimum
    maxX = (($partitionMeshSummaries | Select-Object -ExpandProperty maxX) | Measure-Object -Maximum).Maximum
    minY = (($partitionMeshSummaries | Select-Object -ExpandProperty minY) | Measure-Object -Minimum).Minimum
    maxY = (($partitionMeshSummaries | Select-Object -ExpandProperty maxY) | Measure-Object -Maximum).Maximum
    minZ = (($partitionMeshSummaries | Select-Object -ExpandProperty minZ) | Measure-Object -Minimum).Minimum
    maxZ = (($partitionMeshSummaries | Select-Object -ExpandProperty maxZ) | Measure-Object -Maximum).Maximum
}

$tioMeshesByLabel = @{}
foreach ($mesh in $partitionSnapshot.meshes) {
    $tioMeshesByLabel[$mesh.label] = Get-TioMeshSummary -Mesh $mesh
}

$comparisons = @(
    (Compare-Stage -Name 'partition->select input' `
        -Phoenix (Get-ObjSummary -Path (Join-Path $PhoenixCaptureDir 'node_4_input_0:input_contrib_0.obj')) `
        -Tio $partitionSummary),
    (Compare-Stage -Name 'select house -> node5 input' `
        -Phoenix (Get-ObjSummary -Path (Join-Path $PhoenixCaptureDir 'node_5_input_0:input_contrib_0.obj')) `
        -Tio $tioMeshesByLabel['house']),
    (Compare-Stage -Name 'select chimney -> node6 input' `
        -Phoenix (Get-ObjSummary -Path (Join-Path $PhoenixCaptureDir 'node_6_input_0:input_contrib_0.obj')) `
        -Tio $tioMeshesByLabel['chimney'])
)

$comparisons | Format-Table -AutoSize
