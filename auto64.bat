@echo off
setlocal EnableDelayedExpansion

:: Configuration
set "EXE=main64.exe"
set "TEST_DIR=testdata"
set "PASS_COUNT=0"
set "FAIL_COUNT=0"

:: Helper Functions
goto :init

:log_header
echo.
echo ========================================================================
echo  %~1
echo ========================================================================
exit /b 0

:log_step
echo [INFO] %~1
exit /b 0

:log_success
echo [OK]   %~1
set /a PASS_COUNT+=1
exit /b 0

:log_error
echo [FAIL] %~1
set /a FAIL_COUNT+=1
goto :error_handler

:check_error
if errorlevel 1 (
    call :log_error "%~1"
) else (
    call :log_success "%~1"
)
exit /b 0

:init
call :log_header "Flippy - Automated Test Suite for x86_64"
echo Date: %date% Time: %time%
echo.

:: --------------------------------------------------------------------------
:: 1. Preparation
:: --------------------------------------------------------------------------
call :log_header "Phase 1: Environment Setup"

:: Clean up any previous runs
if exist "%TEST_DIR%" rmdir /s /q "%TEST_DIR%" >nul 2>&1
if exist "temp.txt" del /q "temp.txt" >nul 2>&1
if exist "iso_file1.txt" del /q "iso_file1.txt" >nul 2>&1
for /d %%D in (out_fd*, iso_out) do if exist "%%D" rmdir /s /q "%%D" >nul 2>&1
for %%F in (*.img, *.iso) do if exist "%%F" del /q "%%F" >nul 2>&1

call :log_step "Creating test directory structure..."
mkdir "%TEST_DIR%"
mkdir "%TEST_DIR%\subdir1"
mkdir "%TEST_DIR%\subdir1\subsub"
mkdir "%TEST_DIR%\subdir2"

echo Root file content > "%TEST_DIR%\root.txt"
echo Subdir1 file content > "%TEST_DIR%\subdir1\file1.txt"
echo Deep nested file > "%TEST_DIR%\subdir1\subsub\deep.txt"
echo Subdir2 file content > "%TEST_DIR%\subdir2\file2.txt"

call :log_success "Test data created."

:: Check if executable exists
if not exist "%EXE%" (
    call :log_error "Executable '%EXE%' not found. Please compile first."
    goto :end_script
)

:: --------------------------------------------------------------------------
:: 2. FAT12 Tests (ALL FORMATS)
:: --------------------------------------------------------------------------
call :log_header "Phase 2: FAT12 Floppy Tests (All Formats)"

:: Added 160, 180, 320, 360 to the list
for %%S in (160 180 320 360 720 1440 2880) do (
    call :log_header "Testing %%S KB Floppy Image"
    
    set "IMG=fd%%S.img"
    set "OUT_DIR=out_fd%%S"

    :: 2.1 Create Image
    call :log_step "Creating %%S KB image..."
    %EXE% create-fd !IMG! %%S
    call :check_error "Create %%S KB Image"
    if errorlevel 1 goto :next_floppy

    :: 2.2 Add Single File
    call :log_step "Adding single file 'temp.txt' to root..."
    echo Temporary test data > temp.txt
    %EXE% add-fd !IMG! temp.txt
    call :check_error "Add single file"

    :: 2.3 List Root (Non-Recursive)
    call :log_step "Listing root directory (non-recursive)..."
    %EXE% list-fd !IMG!
    echo.

    :: 2.4 Add Directory Recursively
    call :log_step "Adding directory '%TEST_DIR%' recursively..."
    %EXE% add-dir-fd !IMG! %TEST_DIR%
    call :check_error "Add directory tree"

    :: 2.5 List Recursive
    call :log_step "Listing full image contents (recursive)..."
    %EXE% list-fd !IMG! -r
    echo.

    :: 2.6 Extract Directory
    call :log_step "Extracting directory 'testdata' to '%OUT_DIR%\'..."
    mkdir "!OUT_DIR!"
    %EXE% extract-dir-fd !IMG! testdata "!OUT_DIR!\testdata"
    call :check_error "Extract directory"
    
    :: Verify Extraction
    if exist "!OUT_DIR!\testdata\root.txt" (
        call :log_success "Verification: root.txt extracted correctly"
    ) else (
        call :log_error "Verification: root.txt missing after extraction"
    )
    if exist "!OUT_DIR!\testdata\subdir1\subsub\deep.txt" (
        call :log_success "Verification: deep nested file extracted correctly"
    ) else (
        call :log_error "Verification: deep nested file missing"
    )

    :: 2.7 Extract Single File
    call :log_step "Extracting single file 'TESTDATA/SUBDIR1/FILE1.TXT'..."
    %EXE% extract-fd !IMG! "TESTDATA/SUBDIR1/FILE1.TXT" "!OUT_DIR!\extracted_single.txt"
    call :check_error "Extract single file"
    
    if exist "!OUT_DIR!\extracted_single.txt" (
        call :log_step "Content of extracted file:"
        type "!OUT_DIR!\extracted_single.txt"
    )

    :: 2.8 Delete File
    call :log_step "Deleting file 'TESTDATA/ROOT.TXT'..."
    %EXE% delete-fd !IMG! "TESTDATA/ROOT.TXT"
    call :check_error "Delete file"

    :: Verify Deletion
    call :log_step "Verifying deletion (Root.txt should be gone)..."
    %EXE% list-fd !IMG! | findstr /I "ROOT.TXT" >nul
    if errorlevel 1 (
        call :log_success "Verification: File successfully deleted"
    ) else (
        call :log_error "Verification: File still exists in listing"
    )

    :: Cleanup for this loop
    call :log_step "Cleaning up temporary files for %%S KB test..."
    del !IMG! >nul 2>&1
    rmdir /s /q "!OUT_DIR!" >nul 2>&1
    
    :next_floppy
    echo.
)

del /q temp.txt >nul 2>&1

:: --------------------------------------------------------------------------
:: 3. ISO 9660 Tests
:: --------------------------------------------------------------------------
call :log_header "Phase 3: ISO 9660 Tests"

set "ISO_IMG=test.iso"
set "ISO_OUT=iso_out"

:: 3.1 Create ISO
call :log_step "Creating ISO from '%TEST_DIR%' with label 'TEST_VOL'..."
%EXE% create-iso %TEST_DIR% %ISO_IMG% "TEST_VOL"
call :check_error "Create ISO Image"
if errorlevel 1 goto :end_script

:: 3.2 List ISO
call :log_step "Listing ISO contents..."
%EXE% list-iso %ISO_IMG%
echo.

:: 3.3 Extract Single File
call :log_step "Extracting 'SUBDIR1/FILE1.TXT' from ISO..."
%EXE% extract-iso %ISO_IMG% "SUBDIR1/FILE1.TXT" "iso_file1.txt"
call :check_error "Extract ISO file"

if exist "iso_file1.txt" (
    call :log_step "Content of extracted ISO file:"
    type "iso_file1.txt"
    call :log_success "Verification: ISO file extracted"
) else (
    call :log_error "Verification: ISO file missing"
)

:: 3.4 Extract Directory
call :log_step "Extracting directory 'SUBDIR1' from ISO..."
mkdir "%ISO_OUT%"
%EXE% extract-dir-iso %ISO_IMG% "SUBDIR1" "%ISO_OUT%\subdir1"
call :check_error "Extract ISO directory"

if exist "%ISO_OUT%\subdir1\file1.txt" (
    call :log_success "Verification: ISO directory extracted"
) else (
    call :log_error "Verification: ISO directory content missing"
)

:: 3.5 Delete File from ISO (New Feature Test)
call :log_step "Testing ISO Deletion: Removing 'SUBDIR2/FILE2.TXT'..."
%EXE% delete-iso %ISO_IMG% "SUBDIR2/FILE2.TXT"
call :check_error "Delete ISO file"

:: Verify Deletion in ISO
call :log_step "Verifying ISO deletion..."
%EXE% list-iso %ISO_IMG% | findstr /I "FILE2.TXT" >nul
if errorlevel 1 (
    call :log_success "Verification: ISO file successfully deleted"
) else (
    call :log_error "Verification: ISO file still exists"
)

:: 3.6 Delete Directory from ISO (New Feature Test)
call :log_step "Testing ISO Dir Deletion: Removing 'SUBDIR1'..."
%EXE% delete-dir-iso %ISO_IMG% "SUBDIR1"
call :check_error "Delete ISO directory"

:: Verify Dir Deletion
call :log_step "Verifying ISO dir deletion..."
%EXE% list-iso %ISO_IMG% | findstr /I "SUBDIR1" >nul
if errorlevel 1 (
    call :log_success "Verification: ISO directory successfully deleted"
) else (
    call :log_error "Verification: ISO directory still exists"
)

:: --------------------------------------------------------------------------
:: 4. Final Cleanup & Summary
:: --------------------------------------------------------------------------
goto :end_script

:error_handler
echo.
echo !!! CRITICAL ERROR OCCURRED. ABORTING TESTS. !!!
echo.

:end_script
call :log_header "Cleanup and Summary"

:: Clean up artifacts
if exist "%TEST_DIR%" rmdir /s /q "%TEST_DIR%" >nul 2>&1
if exist "temp.txt" del /q "temp.txt" >nul 2>&1
if exist "iso_file1.txt" del /q "iso_file1.txt" >nul 2>&1
if exist "%ISO_OUT%" rmdir /s /q "%ISO_OUT%" >nul 2>&1
for %%F in (*.img, *.iso) do if exist "%%F" del /q "%%F" >nul 2>&1

call :log_success "Temporary files cleaned."

echo.
echo ========================================================================
echo  TEST SUMMARY
echo ========================================================================
echo  Passed: %PASS_COUNT%
echo  Failed: %FAIL_COUNT%
echo ========================================================================

if %FAIL_COUNT% GTR 0 (
    echo  RESULT: SOME TESTS FAILED
    endlocal
    exit /b 1
) else (
    echo  RESULT: ALL TESTS PASSED
    endlocal
    exit /b 0
)
