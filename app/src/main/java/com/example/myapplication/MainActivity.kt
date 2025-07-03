package com.example.myapplication

import android.Manifest
import android.app.Activity
import android.app.NativeActivity
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) {
            ActivityCompat.requestPermissions(
                this,
                arrayOf<String>(Manifest.permission.CAMERA),
                CAMERA_REQUEST_CODE
            )
        } else {
            launchNativeActivity()
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        results: IntArray
    ) {
        if (requestCode == CAMERA_REQUEST_CODE && results.size > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
            launchNativeActivity()
        } else {
            Toast.makeText(this, "Camera permission denied", Toast.LENGTH_LONG).show()
            finish()
        }
    }

    private fun launchNativeActivity() {
        val intent = Intent(this, NativeActivity::class.java)
        startActivity(intent)
        finish()
    }

    companion object {
        private const val CAMERA_REQUEST_CODE = 1
    }
}