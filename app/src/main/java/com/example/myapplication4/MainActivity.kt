package com.example.u3d

import android.view.MotionEvent
import com.google.androidgamesdk.GameActivity
import kotlin.math.abs

class MainActivity : GameActivity() {

    private var lastX = 0f
    private var lastY = 0f
    private var dragging = false

    companion object {
        private const val ROTATION_SCALE = 0.005f
        private const val MOVE_THRESHOLD = 0.5f

        init {
            System.loadLibrary("u3d")
        }
    }

    external fun nativeRotate(dx: Float, dy: Float)

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> startDrag(event)
            MotionEvent.ACTION_MOVE -> updateDrag(event)
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_CANCEL -> stopDrag()
        }
        return true
    }

    private fun startDrag(event: MotionEvent) {
        lastX = event.x
        lastY = event.y
        dragging = true
    }

    private fun updateDrag(event: MotionEvent) {
        if (!dragging) return

        val dx = event.x - lastX
        val dy = event.y - lastY

        lastX = event.x
        lastY = event.y

        if (abs(dx) > MOVE_THRESHOLD || abs(dy) > MOVE_THRESHOLD) {
            nativeRotate(dx * ROTATION_SCALE, dy * ROTATION_SCALE)
        }
    }

    private fun stopDrag() {
        dragging = false
    }
}
