New-Item -ItemType Directory -Force 'c:\Users\ProfitVault\Downloads\waste-bin-monitoring\tmp_doc' | Out-Null
Copy-Item 'c:\Users\ProfitVault\Downloads\waste-bin-monitoring\IoT_System_Full_Documentation.docx' 'c:\Users\ProfitVault\Downloads\waste-bin-monitoring\tmp_doc\doc.zip' -Force
Expand-Archive -Force 'c:\Users\ProfitVault\Downloads\waste-bin-monitoring\tmp_doc\doc.zip' 'c:\Users\ProfitVault\Downloads\waste-bin-monitoring\tmp_doc\out'
$xml = Get-Content 'c:\Users\ProfitVault\Downloads\waste-bin-monitoring\tmp_doc\out\word\document.xml' -Raw
$text = $xml -replace '<[^>]+>', ' '
$text = $text -replace '\s+', ' '
$text | Out-File 'c:\Users\ProfitVault\Downloads\waste-bin-monitoring\tmp_doc\iot_text.txt' -Encoding utf8
Write-Host "Done - saved to tmp_doc\iot_text.txt"
